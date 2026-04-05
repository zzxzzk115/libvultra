#include "vultra/core/rhi/graphics_pipeline.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/backends/webgpu/conversions.hpp"
#include "vultra/core/rhi/backends/webgpu/webgpu_pipeline.hpp"
#include "vultra/core/rhi/backends/webgpu/webgpu_render_device_access.hpp"
#include "vultra/core/rhi/pipeline_layout.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/core/rhi/shader_reflection.hpp"
#include "vultra/core/rhi/util.hpp"

#include <algorithm>

#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
#include <webgpu/webgpu.h>
#endif

namespace vultra
{
    namespace rhi
    {
        namespace
        {
            [[nodiscard]] ShaderReflection mergeReflections(const ShaderReflection& lhs, const ShaderReflection& rhs)
            {
                ShaderReflection merged = lhs;
                for (size_t set = 0; set < merged.descriptorSets.size(); ++set)
                {
                    for (const auto& [binding, desc] : rhs.descriptorSets[set])
                    {
                        auto it = merged.descriptorSets[set].find(binding);
                        if (it == merged.descriptorSets[set].end())
                        {
                            merged.descriptorSets[set].emplace(binding, desc);
                            continue;
                        }

                        it->second.stageFlags |= desc.stageFlags;
                        it->second.flags |= desc.flags;
                        it->second.count = std::max(it->second.count, desc.count);
                    }
                }

                for (const auto& rhsRange : rhs.pushConstantRanges)
                {
                    bool mergedExisting = false;
                    for (auto& lhsRange : merged.pushConstantRanges)
                    {
                        if (lhsRange.offset == rhsRange.offset && lhsRange.size == rhsRange.size)
                        {
                            lhsRange.stageFlags |= rhsRange.stageFlags;
                            mergedExisting = true;
                            break;
                        }
                    }
                    if (!mergedExisting)
                    {
                        merged.pushConstantRanges.push_back(rhsRange);
                    }
                }
                return merged;
            }
        } // namespace

        std::optional<GraphicsPipeline> GraphicsPipeline::Builder::buildWebGPU(RenderDevice& rd)
        {
#if !defined(VULTRA_ENABLE_WEBGPU) || !VULTRA_ENABLE_WEBGPU
            (void)rd;
            return std::nullopt;
#else
            [[maybe_unused]] static auto createWgpuShaderModule = [](WGPUDevice device, const std::string_view wgsl) {
                WGPUShaderSourceWGSL source {};
                source.chain.sType = WGPUSType_ShaderSourceWGSL;
                source.code        = WGPUStringView {.data = wgsl.data(), .length = WGPU_STRLEN};

                WGPUShaderModuleDescriptor descriptor {};
                descriptor.nextInChain = const_cast<WGPUChainedStruct*>(
                    reinterpret_cast<const WGPUChainedStruct*>(&source));
                return wgpuDeviceCreateShaderModule(device, &descriptor);
            };

            auto* const device = reinterpret_cast<WGPUDevice>(WebGPURenderDeviceAccess::getDeviceHandle(rd));
            if (device == nullptr)
            {
                return std::nullopt;
            }

            const auto vertexIt = m_ShaderStages.find(ShaderType::eVertex);
            const auto fragIt   = m_ShaderStages.find(ShaderType::eFragment);
            if (vertexIt == m_ShaderStages.end() || fragIt == m_ShaderStages.end())
            {
                VULTRA_CORE_ERROR("[GraphicsPipeline] WebGPU requires vertex + fragment shader stages");
                return std::nullopt;
            }

            auto vertexModule   = rd.createShaderModule(ShaderType::eVertex,
                                                      vertexIt->second.code,
                                                      vertexIt->second.entryPointName,
                                                      vertexIt->second.defines,
                                                      nullptr);
            auto fragmentModule = rd.createShaderModule(ShaderType::eFragment,
                                                        fragIt->second.code,
                                                        fragIt->second.entryPointName,
                                                        fragIt->second.defines,
                                                        nullptr);
            if (!vertexModule || !fragmentModule)
            {
                return std::nullopt;
            }
            if (vertexIt->second.reflection.has_value())
            {
                vertexModule.getReflection() = *vertexIt->second.reflection;
            }
            if (fragIt->second.reflection.has_value())
            {
                fragmentModule.getReflection() = *fragIt->second.reflection;
            }

            auto* const wgpuVertexModule   = createWgpuShaderModule(device, vertexModule.getWgsl());
            auto* const wgpuFragmentModule = createWgpuShaderModule(device, fragmentModule.getWgsl());
            if (wgpuVertexModule == nullptr || wgpuFragmentModule == nullptr)
            {
                if (wgpuVertexModule)
                    wgpuShaderModuleRelease(wgpuVertexModule);
                if (wgpuFragmentModule)
                    wgpuShaderModuleRelease(wgpuFragmentModule);
                return std::nullopt;
            }

            std::vector<WGPUVertexAttribute> wgpuVertexAttributes;
            wgpuVertexAttributes.reserve(m_VertexAttributes.size());
            uint64_t inferredVertexStride = 0;
            for (const auto& [location, attrib] : m_VertexAttributes)
            {
                if (attrib.offset == kIgnoreVertexAttribute)
                {
                    continue;
                }
                WGPUVertexAttribute wgpuAttrib {};
                wgpuAttrib.shaderLocation = location;
                wgpuAttrib.offset         = attrib.offset;
                wgpuAttrib.format         = webgpu::toWgpuVertexFormat(attrib.type);
                wgpuVertexAttributes.push_back(wgpuAttrib);
                inferredVertexStride = std::max<uint64_t>(inferredVertexStride, attrib.offset + getSize(attrib.type));
            }
            const uint64_t vertexStride = m_VertexStride > 0 ? m_VertexStride : inferredVertexStride;

            WGPUVertexBufferLayout vertexBufferLayout {};
            vertexBufferLayout.stepMode       = WGPUVertexStepMode_Vertex;
            vertexBufferLayout.arrayStride    = vertexStride;
            vertexBufferLayout.attributeCount = wgpuVertexAttributes.size();
            vertexBufferLayout.attributes     = wgpuVertexAttributes.data();

            WGPUVertexState vertexState {};
            vertexState.module     = wgpuVertexModule;
            vertexState.entryPoint = WGPUStringView {.data   = vertexIt->second.entryPointName.data(),
                                                     .length = vertexIt->second.entryPointName.size()};
            if (!wgpuVertexAttributes.empty())
            {
                vertexState.bufferCount = 1;
                vertexState.buffers     = &vertexBufferLayout;
            }

            const auto           colorFormat = !m_ColorAttachmentFormats.empty() ?
                                                   webgpu::toWgpuTextureFormat(m_ColorAttachmentFormats.front()) :
                                                   WGPUTextureFormat_BGRA8UnormSrgb;
            WGPUColorTargetState colorTarget {};
            colorTarget.format    = colorFormat;
            colorTarget.writeMask = WGPUColorWriteMask_All;

            WGPUBlendState blendState {};
            const bool     hasBlendState = !m_BlendStates.empty() && m_BlendStates.front().enabled;
            if (hasBlendState)
            {
                // Keep WebGPU blend policy minimal for now: standard alpha blend.
                blendState.color.operation = WGPUBlendOperation_Add;
                blendState.color.srcFactor = WGPUBlendFactor_SrcAlpha;
                blendState.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
                blendState.alpha.operation = WGPUBlendOperation_Add;
                blendState.alpha.srcFactor = WGPUBlendFactor_One;
                blendState.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
                colorTarget.blend          = &blendState;
            }

            WGPUFragmentState fragmentState {};
            fragmentState.module      = wgpuFragmentModule;
            fragmentState.entryPoint  = WGPUStringView {.data   = fragIt->second.entryPointName.data(),
                                                        .length = fragIt->second.entryPointName.size()};
            fragmentState.targetCount = 1;
            fragmentState.targets     = &colorTarget;

            WGPUPipelineLayout wgpuLayout {nullptr};
            if (m_PipelineLayout)
            {
                wgpuLayout = reinterpret_cast<WGPUPipelineLayout>(m_PipelineLayout.getHandle());
            }
            else
            {
                const auto mergedReflection =
                    mergeReflections(vertexModule.getReflection(), fragmentModule.getReflection());
                m_PipelineLayout = reflectPipelineLayout(rd, mergedReflection);
                if (m_PipelineLayout)
                {
                    wgpuLayout = reinterpret_cast<WGPUPipelineLayout>(m_PipelineLayout.getHandle());
                }
            }

            WGPURenderPipelineDescriptor descriptor {};
            descriptor.layout                             = wgpuLayout;
            descriptor.vertex                             = vertexState;
            descriptor.primitive.topology                 = webgpu::toWgpuPrimitiveTopology(m_PrimitiveTopology);
            descriptor.primitive.frontFace                = WGPUFrontFace_CCW;
            descriptor.primitive.cullMode                 = webgpu::toWgpuCullMode(m_RasterizerState.cullMode);
            descriptor.multisample.count                  = 1;
            descriptor.multisample.mask                   = ~0u;
            descriptor.multisample.alphaToCoverageEnabled = false;
            descriptor.fragment                           = &fragmentState;

            WGPUDepthStencilState depthStencilState {};
            if (m_DepthFormat != PixelFormat::eUndefined)
            {
                const auto depthFormat = webgpu::toWgpuTextureFormat(m_DepthFormat);
                if (depthFormat == WGPUTextureFormat_Undefined)
                {
                    VULTRA_CORE_ERROR("[GraphicsPipeline] Unsupported WebGPU depth format");
                    wgpuShaderModuleRelease(wgpuVertexModule);
                    wgpuShaderModuleRelease(wgpuFragmentModule);
                    return std::nullopt;
                }

                depthStencilState.format = depthFormat;
                depthStencilState.depthWriteEnabled =
                    m_DepthStencilState.depthWrite ? WGPUOptionalBool_True : WGPUOptionalBool_False;
                depthStencilState.depthCompare        = m_DepthStencilState.depthTest ?
                                                            webgpu::toWgpuCompareFunction(m_DepthStencilState.depthCompareOp) :
                                                            WGPUCompareFunction_Always;
                depthStencilState.depthBias           = 0;
                depthStencilState.depthBiasSlopeScale = 0.0f;
                depthStencilState.depthBiasClamp      = 0.0f;
                descriptor.depthStencil               = &depthStencilState;
            }

            auto* const pipeline = wgpuDeviceCreateRenderPipeline(device, &descriptor);
            wgpuShaderModuleRelease(wgpuVertexModule);
            wgpuShaderModuleRelease(wgpuFragmentModule);

            if (pipeline == nullptr)
            {
                return std::nullopt;
            }

            return GraphicsPipeline {
                std::move(m_PipelineLayout),
                reinterpret_cast<std::uintptr_t>(pipeline),
                std::make_unique<WebGPUPipeline>(),
            };
#endif
        }
    } // namespace rhi
} // namespace vultra
