#include "vultra/core/rhi/backends/webgpu/webgpu_command_buffer.hpp"
#include "vultra/core/rhi/backends/webgpu/conversions.hpp"
#include "vultra/core/rhi/backends/webgpu/webgpu_descriptor_set.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/base/visitor_helper.hpp"
#include "vultra/core/profiling/tracky.hpp"
#include "vultra/core/rhi/backends/webgpu/webgpu_swapchain.hpp"
#include "vultra/core/rhi/descriptorset_builder.hpp"
#include "vultra/core/rhi/draw_indirect_buffer.hpp"
#include "vultra/core/rhi/graphics_pipeline.hpp"
#include "vultra/core/rhi/index_buffer.hpp"
#include "vultra/core/rhi/interfaces/idescriptor_set_builder.hpp"
#include "vultra/core/rhi/interfaces/texture_access.hpp"
#include "vultra/core/rhi/structs/pipeline_layout_structs.hpp"
#include "vultra/core/rhi/structs/pixel_format.hpp"
#include "vultra/core/rhi/texture.hpp"
#include "vultra/core/rhi/vertex_buffer.hpp"

#include <cstring>
#include <stdexcept>
#include <string>
#include <unordered_map>

#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
#include <webgpu/webgpu.h>
#endif

namespace vultra
{
    namespace rhi
    {
        namespace
        {
            constexpr DescriptorSetIndex kWebGPUPushConstantsSet        = 1u;
            constexpr BindingIndex       kWebGPUPushConstantsBinding    = 31u;
            constexpr uint64_t           kWebGPUPushConstantBufferBytes = 256u;

            class WebGPUDescriptorSetBuilder final : public IDescriptorSetBuilder
            {
            public:
                explicit WebGPUDescriptorSetBuilder(std::vector<std::unique_ptr<WebGPUDescriptorSet>>& storage) :
                    m_Storage(storage)
                {}

                void bind(const BindingIndex index, const ResourceBinding& binding) override
                {
                    m_Bindings[index] = binding;
                }
                void bind(const BindingIndex index, const bindings::SeparateSampler& value) override
                {
                    m_Bindings[index] = value;
                }
                void bind(const BindingIndex index, const bindings::CombinedImageSampler& value) override
                {
                    m_Bindings[index] = value;
                }
                void bind(const BindingIndex index, const bindings::CombinedImageSamplerArray& value) override
                {
                    m_Bindings[index] = value;
                }
                void bind(const BindingIndex index, const bindings::SampledImage& value) override
                {
                    m_Bindings[index] = value;
                }
                void bind(const BindingIndex index, const bindings::StorageImage& value) override
                {
                    m_Bindings[index] = value;
                }
                void bind(const BindingIndex index, const bindings::UniformBuffer& value) override
                {
                    m_Bindings[index] = value;
                }
                void bind(const BindingIndex index, const bindings::StorageBuffer& value) override
                {
                    m_Bindings[index] = value;
                }
                void bind(const BindingIndex index, const bindings::AccelerationStructureKHR& value) override
                {
                    m_Bindings[index] = value;
                }

                [[nodiscard]] DescriptorSetHandle build(const DescriptorSetLayoutKey layoutKey) override
                {
                    auto  descriptorSet = std::make_unique<WebGPUDescriptorSet>(layoutKey, std::move(m_Bindings));
                    auto* handle        = descriptorSet.get();
                    m_Storage.emplace_back(std::move(descriptorSet));
                    return DescriptorSetHandle {reinterpret_cast<std::uintptr_t>(handle)};
                }

            private:
                std::unordered_map<BindingIndex, ResourceBinding>  m_Bindings;
                std::vector<std::unique_ptr<WebGPUDescriptorSet>>& m_Storage;
            };

            [[nodiscard]] WGPUColor toWgpuColor(const std::optional<ClearValue>& clearValue)
            {
                if (!clearValue.has_value())
                {
                    return WGPUColor {.r = 0.0, .g = 0.0, .b = 0.0, .a = 1.0};
                }

                return std::visit(
                    Overload {
                        [](const glm::vec4& v) { return WGPUColor {.r = v.x, .g = v.y, .b = v.z, .a = v.w}; },
                        [](const glm::ivec4& v) {
                            return WGPUColor {.r = static_cast<float>(v.x),
                                              .g = static_cast<float>(v.y),
                                              .b = static_cast<float>(v.z),
                                              .a = static_cast<float>(v.w)};
                        },
                        [](const glm::uvec4& v) {
                            return WGPUColor {.r = static_cast<float>(v.x),
                                              .g = static_cast<float>(v.y),
                                              .b = static_cast<float>(v.z),
                                              .a = static_cast<float>(v.w)};
                        },
                        [](const float v) { return WGPUColor {.r = v, .g = 0.0, .b = 0.0, .a = 1.0}; },
                        [](const uint32_t v) {
                            return WGPUColor {.r = static_cast<float>(v), .g = 0.0, .b = 0.0, .a = 1.0};
                        },
                    },
                    *clearValue);
            }

            [[nodiscard]] float toWgpuDepthClear(const std::optional<ClearValue>& clearValue)
            {
                if (!clearValue.has_value())
                {
                    return 1.0f;
                }

                return std::visit(Overload {
                                      [](const glm::vec4& v) { return v.x; },
                                      [](const glm::ivec4& v) { return static_cast<float>(v.x); },
                                      [](const glm::uvec4& v) { return static_cast<float>(v.x); },
                                      [](const float v) { return v; },
                                      [](const uint32_t v) { return static_cast<float>(v); },
                                  },
                                  *clearValue);
            }

            [[nodiscard]] uint32_t toWgpuStencilClear(const std::optional<ClearValue>& clearValue)
            {
                if (!clearValue.has_value())
                {
                    return 0u;
                }

                return std::visit(Overload {
                                      [](const glm::vec4& v) { return static_cast<uint32_t>(v.y); },
                                      [](const glm::ivec4& v) { return static_cast<uint32_t>(v.y); },
                                      [](const glm::uvec4& v) { return v.y; },
                                      [](const float) { return 0u; },
                                      [](const uint32_t v) { return v; },
                                  },
                                  *clearValue);
            }

        } // namespace

        WebGPUCommandBuffer::WebGPUCommandBuffer(const WebGPURenderDevice& backend) :
            m_Instance(backend.m_Instance), m_Device(backend.m_Device), m_Queue(backend.m_Queue),
            m_Backend(const_cast<WebGPURenderDevice*>(&backend))
        {}

        WebGPUCommandBuffer::~WebGPUCommandBuffer() { releaseTransientResources(); }

        std::uintptr_t WebGPUCommandBuffer::getHandle() const { return reinterpret_cast<std::uintptr_t>(m_Encoder); }

        TracyGpuContext WebGPUCommandBuffer::getTracyContext() const { return nullptr; }

        std::uintptr_t WebGPUCommandBuffer::getCurrentRenderPassEncoderHandle() const
        {
            return reinterpret_cast<std::uintptr_t>(m_RenderPass);
        }

        std::uintptr_t WebGPUCommandBuffer::getCurrentComputePassEncoderHandle() const
        {
            return reinterpret_cast<std::uintptr_t>(m_ComputePass);
        }

        void WebGPUCommandBuffer::closeActiveComputePassForProfilingBoundary()
        {
#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
            if (m_ComputePass == nullptr)
            {
                return;
            }

            wgpuComputePassEncoderEnd(m_ComputePass);
            if (m_Backend != nullptr)
            {
                m_Backend->finalizePendingPassTimestampQueries(m_Encoder ? reinterpret_cast<std::uintptr_t>(m_Encoder) :
                                                                           0u);
            }
            wgpuComputePassEncoderRelease(m_ComputePass);
            m_ComputePass = nullptr;
#endif
        }

        Barrier::Builder& WebGPUCommandBuffer::getBarrierBuilder() { return m_BarrierBuilder; }

        DescriptorSetBuilder WebGPUCommandBuffer::createDescriptorSetBuilder()
        {
            return DescriptorSetBuilder {std::make_unique<WebGPUDescriptorSetBuilder>(m_DescriptorSets)};
        }

        WebGPUCommandBuffer& WebGPUCommandBuffer::begin()
        {
            if (m_Recording)
            {
                return *this;
            }

#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
            if (m_Device == nullptr)
            {
                throw std::runtime_error("WebGPUCommandBuffer begin failed: invalid WebGPU device");
            }

            WGPUCommandEncoderDescriptor encoderDesc {};
            m_Encoder = wgpuDeviceCreateCommandEncoder(m_Device, &encoderDesc);
            if (m_Encoder == nullptr)
            {
                throw std::runtime_error(
                    "WebGPUCommandBuffer begin failed: wgpuDeviceCreateCommandEncoder returned null");
            }
#endif
            m_Recording                  = true;
            m_InsideRendering            = false;
            m_PipelineBoundInCurrentPass = false;
            m_BoundComputePipeline       = nullptr;
            m_PendingRenderBindGroups.fill(nullptr);
            m_PendingComputeBindGroups.fill(nullptr);
            TRACKY_BIND_CMD_BUFFER(getHandle(), 0, 0);
            return *this;
        }

        WebGPUCommandBuffer& WebGPUCommandBuffer::end()
        {
            if (!m_Recording)
            {
                return *this;
            }
            if (m_InsideRendering)
            {
                endRendering();
            }
#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
            if (m_ComputePass != nullptr)
            {
                closeActiveComputePassForProfilingBoundary();
            }
            TRACKY_BIND_CMD_BUFFER(
                getHandle(), getCurrentRenderPassEncoderHandle(), getCurrentComputePassEncoderHandle());
#ifdef TRACKY_ENABLE
            tracky::resolve_webgpu_queries();
#endif
#endif
            m_Recording = false;
            return *this;
        }

        WebGPUCommandBuffer& WebGPUCommandBuffer::reset()
        {
            releaseTransientResources();
            m_Recording                  = false;
            m_InsideRendering            = false;
            m_SkipCurrentRendering       = false;
            m_BoundPipeline              = nullptr;
            m_BoundComputePipeline       = nullptr;
            m_BoundPipelineObject        = nullptr;
            m_BarrierBuilder             = Barrier::Builder {};
            m_OwnsRenderView             = false;
            m_OwnsDepthView              = false;
            m_PipelineBoundInCurrentPass = false;
            m_PendingRenderBindGroups.fill(nullptr);
            m_PendingComputeBindGroups.fill(nullptr);
            TRACKY_BIND_CMD_BUFFER(0, 0, 0);
            return *this;
        }

        WebGPUCommandBuffer& WebGPUCommandBuffer::submit(const JobInfo&, const bool)
        {
#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
            if (m_Encoder == nullptr)
            {
                return *this;
            }
            if (m_InsideRendering)
            {
                endRendering();
            }
            if (m_Recording)
            {
                end();
            }

            WGPUCommandBuffer commandBuffer = wgpuCommandEncoderFinish(m_Encoder, nullptr);
            if (commandBuffer != nullptr)
            {
                if (m_Queue != nullptr)
                {
                    wgpuQueueSubmit(m_Queue, 1, &commandBuffer);
                }
                wgpuCommandBufferRelease(commandBuffer);
            }
#endif
            TRACKY_BIND_CMD_BUFFER(0, 0, 0);
            releaseTransientResources();
            m_BarrierBuilder = Barrier::Builder {};
            return *this;
        }

        WebGPUCommandBuffer& WebGPUCommandBuffer::bindPipeline(const BasePipeline& pipeline)
        {
            m_BoundPipelineObject = &pipeline;
            if (pipeline.getBindPoint() == PipelineBindPoint::eCompute)
            {
                m_BoundComputePipeline = reinterpret_cast<WGPUComputePipeline>(pipeline.getHandle());
                m_BoundPipeline        = nullptr;
#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
                if (m_ComputePass != nullptr && m_BoundComputePipeline != nullptr)
                {
                    wgpuComputePassEncoderSetPipeline(m_ComputePass, m_BoundComputePipeline);
                }
#endif
                return *this;
            }

            m_BoundPipeline        = reinterpret_cast<WGPURenderPipeline>(pipeline.getHandle());
            m_BoundComputePipeline = nullptr;
#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
            if (m_InsideRendering && m_RenderPass != nullptr && m_BoundPipeline != nullptr)
            {
                wgpuRenderPassEncoderSetPipeline(m_RenderPass, m_BoundPipeline);
                m_PipelineBoundInCurrentPass = true;
            }
#endif
            return *this;
        }

        WebGPUCommandBuffer& WebGPUCommandBuffer::dispatch(const ComputePipeline& pipeline,
                                                           const glm::uvec3&      groupCount)
        {
            bindPipeline(pipeline);
            return dispatch(groupCount);
        }
        WebGPUCommandBuffer& WebGPUCommandBuffer::dispatch(const glm::uvec3& groupCount)
        {
#if !defined(VULTRA_ENABLE_WEBGPU) || !VULTRA_ENABLE_WEBGPU
            (void)groupCount;
            return *this;
#else
            if (m_Encoder == nullptr || m_BoundComputePipeline == nullptr || m_BoundPipelineObject == nullptr)
            {
                return *this;
            }
            if (m_InsideRendering)
            {
                throw std::runtime_error("WebGPUCommandBuffer dispatch failed: inside render pass");
            }
            if (m_ComputePass == nullptr)
            {
                WGPUComputePassDescriptor descriptor {};
#if defined(__EMSCRIPTEN__)
                WGPUPassTimestampWrites timestampWrites {};
#else
                WGPUComputePassTimestampWrites timestampWrites {};
#endif
                if (m_Backend)
                {
                    WGPUQuerySet querySet {nullptr};
                    uint32_t     beginIndex {0};
                    uint32_t     endIndex {0};
                    if (m_Backend->consumePassTimestampWriteRequest(querySet, beginIndex, endIndex))
                    {
                        timestampWrites.querySet                  = querySet;
                        timestampWrites.beginningOfPassWriteIndex = beginIndex;
                        timestampWrites.endOfPassWriteIndex       = endIndex;
                        descriptor.timestampWrites                = &timestampWrites;
                    }
                }
                m_ComputePass = wgpuCommandEncoderBeginComputePass(m_Encoder, &descriptor);
                if (m_ComputePass == nullptr)
                {
                    throw std::runtime_error("WebGPUCommandBuffer dispatch failed: cannot begin compute pass");
                }
            }

            wgpuComputePassEncoderSetPipeline(m_ComputePass, m_BoundComputePipeline);
            for (DescriptorSetIndex set = 0; set < kMinNumDescriptorSets; ++set)
            {
                if (m_PendingComputeBindGroups[set] != nullptr)
                {
                    wgpuComputePassEncoderSetBindGroup(m_ComputePass, set, m_PendingComputeBindGroups[set], 0, nullptr);
                }
            }
            wgpuComputePassEncoderDispatchWorkgroups(m_ComputePass, groupCount.x, groupCount.y, groupCount.z);
            return *this;
#endif
        }
        WebGPUCommandBuffer& WebGPUCommandBuffer::dispatchIndirect(const Buffer&, uint64_t)
        {
            unsupported("dispatchIndirect");
        }
        WebGPUCommandBuffer& WebGPUCommandBuffer::insertComputeUavBarrier() { return *this; }
        WebGPUCommandBuffer& WebGPUCommandBuffer::traceRays(const ShaderBindingTable&, const glm::uvec3&)
        {
            unsupported("traceRays");
        }
        WebGPUCommandBuffer& WebGPUCommandBuffer::bindDescriptorSet(const DescriptorSetIndex  index,
                                                                    const DescriptorSetHandle descriptorSet)
        {
#if !defined(VULTRA_ENABLE_WEBGPU) || !VULTRA_ENABLE_WEBGPU
            (void)index;
            (void)descriptorSet;
            return *this;
#else
            if (m_BoundPipelineObject == nullptr || m_Backend == nullptr || !descriptorSet)
            {
                return *this;
            }

            auto* setData = reinterpret_cast<WebGPUDescriptorSet*>(descriptorSet.value);
            if (setData == nullptr)
            {
                return *this;
            }

            const auto expectedLayoutKey = m_BoundPipelineObject->getDescriptorSetLayout(index);
            if (!expectedLayoutKey)
            {
                return *this;
            }

            if (setData->layoutKey() && setData->layoutKey().value != expectedLayoutKey.value)
            {
                VULTRA_CORE_WARN(
                    "[WebGPUCommandBuffer] DescriptorSet layout mismatch at set={} (built={}, expected={})",
                    index,
                    setData->layoutKey().value,
                    expectedLayoutKey.value);
            }

            auto* const bindGroup = setData->getOrCreateBindGroup(*m_Backend, expectedLayoutKey);
            if (bindGroup == nullptr)
            {
                return *this;
            }
            if (m_BoundPipeline != nullptr)
            {
                m_PendingRenderBindGroups[index] = bindGroup;
                if (m_RenderPass != nullptr)
                {
                    wgpuRenderPassEncoderSetBindGroup(m_RenderPass, index, bindGroup, 0, nullptr);
                }
            }
            else if (m_BoundComputePipeline != nullptr)
            {
                if (m_ComputePass != nullptr)
                {
                    wgpuComputePassEncoderSetBindGroup(m_ComputePass, index, bindGroup, 0, nullptr);
                }
                m_PendingComputeBindGroups[index] = bindGroup;
            }
            return *this;
#endif
        }
        WebGPUCommandBuffer&
        WebGPUCommandBuffer::pushConstants(ShaderStages stages, uint32_t offset, uint32_t size, const void* data)
        {
#if !defined(VULTRA_ENABLE_WEBGPU) || !VULTRA_ENABLE_WEBGPU
            (void)stages;
            (void)offset;
            (void)size;
            (void)data;
            return *this;
#else
            if (m_Device == nullptr || m_Queue == nullptr || m_BoundPipelineObject == nullptr || data == nullptr ||
                size == 0)
            {
                return *this;
            }
            (void)stages;

            const auto layoutKey = m_BoundPipelineObject->getDescriptorSetLayout(kWebGPUPushConstantsSet);
            if (!layoutKey)
            {
                return *this;
            }

            const auto bindingIt = m_Backend->m_DescriptorSetLayoutBindings.find(layoutKey.value);
            if (bindingIt == m_Backend->m_DescriptorSetLayoutBindings.end())
            {
                return *this;
            }

            bool hasPushConstantBinding = false;
            for (const auto& binding : bindingIt->second)
            {
                if (binding.binding == kWebGPUPushConstantsBinding && binding.type == DescriptorType::eUniformBuffer)
                {
                    hasPushConstantBinding = true;
                    break;
                }
            }
            if (!hasPushConstantBinding)
            {
                return *this;
            }

            const uint64_t requiredSize = std::max<uint64_t>(kWebGPUPushConstantBufferBytes, offset + size);
            if (m_PushConstantBuffer == nullptr || m_PushConstantBufferSize < requiredSize)
            {
                for (auto& [_, bindGroup] : m_PushConstantBindGroups)
                {
                    if (bindGroup != nullptr)
                    {
                        wgpuBindGroupRelease(bindGroup);
                    }
                }
                m_PushConstantBindGroups.clear();
                if (m_PushConstantBuffer != nullptr)
                {
                    wgpuBufferRelease(m_PushConstantBuffer);
                }

                WGPUBufferDescriptor descriptor {};
                descriptor.usage         = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Uniform;
                descriptor.size          = requiredSize;
                m_PushConstantBuffer     = wgpuDeviceCreateBuffer(m_Device, &descriptor);
                m_PushConstantBufferSize = requiredSize;
            }
            if (m_PushConstantBuffer == nullptr)
            {
                return *this;
            }
            // Performance-oriented emulation: WebGPU push constants are represented by a tiny
            // uniform buffer updated via queue write (similar to Bevy's dynamic-uniform workflow).
            wgpuQueueWriteBuffer(m_Queue, m_PushConstantBuffer, offset, data, size);

            auto it = m_PushConstantBindGroups.find(layoutKey.value);
            if (it == m_PushConstantBindGroups.end())
            {
                const auto layoutIt = m_Backend->m_DescriptorSetLayouts.find(layoutKey.value);
                if (layoutIt == m_Backend->m_DescriptorSetLayouts.end())
                {
                    return *this;
                }

                WGPUBindGroupEntry entry {};
                entry.binding = kWebGPUPushConstantsBinding;
                entry.buffer  = m_PushConstantBuffer;
                entry.offset  = 0;
                entry.size    = m_PushConstantBufferSize;

                WGPUBindGroupDescriptor bindGroupDesc {};
                bindGroupDesc.layout     = layoutIt->second;
                bindGroupDesc.entryCount = 1;
                bindGroupDesc.entries    = &entry;

                auto* const bindGroup = wgpuDeviceCreateBindGroup(m_Device, &bindGroupDesc);
                if (bindGroup == nullptr)
                {
                    return *this;
                }
                it = m_PushConstantBindGroups.emplace(layoutKey.value, bindGroup).first;
            }

            if (m_RenderPass != nullptr && m_BoundPipeline != nullptr)
            {
                wgpuRenderPassEncoderSetBindGroup(m_RenderPass, kWebGPUPushConstantsSet, it->second, 0, nullptr);
            }
            else if (m_BoundComputePipeline != nullptr)
            {
                if (m_ComputePass != nullptr)
                {
                    wgpuComputePassEncoderSetBindGroup(m_ComputePass, kWebGPUPushConstantsSet, it->second, 0, nullptr);
                }
                m_PendingComputeBindGroups[kWebGPUPushConstantsSet] = it->second;
            }
            return *this;
#endif
        }

        WebGPUCommandBuffer& WebGPUCommandBuffer::beginRendering(const FramebufferInfo& framebufferInfo)
        {
            if (m_Encoder == nullptr)
            {
                throw std::runtime_error(
                    "WebGPUCommandBuffer beginRendering failed: encoder is null, call begin() first");
            }
            if (m_InsideRendering)
            {
                throw std::runtime_error("WebGPUCommandBuffer beginRendering failed: already inside rendering");
            }
#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
            if (m_ComputePass != nullptr)
            {
                closeActiveComputePassForProfilingBoundary();
            }
#endif

            // WebGPU texture backend is still being completed.
            // If the requested color target is unavailable (null/invalid handle), skip this pass safely.
            if (framebufferInfo.colorAttachments.empty() ||
                framebufferInfo.colorAttachments.front().target == nullptr ||
                TextureAccess::getImageHandle(*framebufferInfo.colorAttachments.front().target) == 0)
            {
                m_SkipCurrentRendering = true;
                m_InsideRendering      = true;
                return *this;
            }

#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
            const auto colorAttachment =
                framebufferInfo.colorAttachments.empty() ? AttachmentInfo {} : framebufferInfo.colorAttachments.front();
            const auto clearColor = toWgpuColor(colorAttachment.clearValue);

            m_RenderView     = nullptr;
            m_DepthView      = nullptr;
            m_OwnsRenderView = false;
            m_OwnsDepthView  = false;

            if (colorAttachment.target != nullptr)
            {
                const auto colorViewHandle = colorAttachment.target->getImageView(ImageAspectFlags::eColor).getHandle();
                m_RenderView               = reinterpret_cast<WGPUTextureView>(colorViewHandle);
            }
            if (m_RenderView == nullptr)
            {
                auto* const currentTexture = getCurrentWebGPUSwapchainTexture();
                if (currentTexture == nullptr)
                {
                    throw std::runtime_error(
                        "WebGPUCommandBuffer beginRendering failed: no acquired swapchain texture");
                }
                m_RenderView     = wgpuTextureCreateView(currentTexture, nullptr);
                m_OwnsRenderView = true;
            }
            if (m_RenderView == nullptr)
            {
                throw std::runtime_error("WebGPUCommandBuffer beginRendering failed: cannot create texture view");
            }

            WGPURenderPassColorAttachment colorDesc {};
            colorDesc.view       = m_RenderView;
            colorDesc.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
            colorDesc.loadOp     = colorAttachment.clearValue.has_value() ? WGPULoadOp_Clear : WGPULoadOp_Load;
            colorDesc.storeOp    = WGPUStoreOp_Store;
            colorDesc.clearValue = clearColor;

            WGPURenderPassDepthStencilAttachment depthDesc {};
            bool                                allowDepthStencilAttachment = true;
            if (m_BoundPipelineObject != nullptr &&
                m_BoundPipelineObject->getBindPoint() == PipelineBindPoint::eGraphics)
            {
                const auto* graphicsPipeline = static_cast<const GraphicsPipeline*>(m_BoundPipelineObject);
                const auto& depthStencil     = graphicsPipeline->getDepthStencilState();
                allowDepthStencilAttachment =
                    graphicsPipeline->getDepthFormat() != PixelFormat::eUndefined ||
                    graphicsPipeline->getStencilFormat() != PixelFormat::eUndefined || depthStencil.depthTest ||
                    depthStencil.depthWrite || depthStencil.stencilTestEnable;
            }

            if (allowDepthStencilAttachment && framebufferInfo.depthAttachment &&
                framebufferInfo.depthAttachment->target != nullptr)
            {
                const auto depthFormat     = framebufferInfo.depthAttachment->target->getPixelFormat();
                const auto depthAspects    = getAspectMask(depthFormat);
                const bool hasStencil      = HasFlagValues(depthAspects, ImageAspectFlags::eStencil);
                const bool stencilReadOnly = framebufferInfo.stencilReadOnly || !hasStencil;

                const auto depthViewHandle =
                    framebufferInfo.depthAttachment->target->getImageView(ImageAspectFlags::eDepth).getHandle();
                m_DepthView = reinterpret_cast<WGPUTextureView>(depthViewHandle);
                if (m_DepthView != nullptr)
                {
                    depthDesc.view = m_DepthView;
                    depthDesc.depthLoadOp =
                        framebufferInfo.depthAttachment->clearValue.has_value() ? WGPULoadOp_Clear : WGPULoadOp_Load;
                    depthDesc.depthStoreOp    = framebufferInfo.depthReadOnly ? WGPUStoreOp_Discard : WGPUStoreOp_Store;
                    depthDesc.depthClearValue = toWgpuDepthClear(framebufferInfo.depthAttachment->clearValue);
                    depthDesc.depthReadOnly   = framebufferInfo.depthReadOnly;

                    depthDesc.stencilReadOnly   = stencilReadOnly;
                    depthDesc.stencilLoadOp     = WGPULoadOp_Undefined;
                    depthDesc.stencilStoreOp    = WGPUStoreOp_Undefined;
                    depthDesc.stencilClearValue = 0u;

                    if (!stencilReadOnly && framebufferInfo.stencilAttachment &&
                        framebufferInfo.stencilAttachment->target != nullptr)
                    {
                        depthDesc.stencilLoadOp     = framebufferInfo.stencilAttachment->clearValue.has_value() ?
                                                          WGPULoadOp_Clear :
                                                          WGPULoadOp_Load;
                        depthDesc.stencilStoreOp    = WGPUStoreOp_Store;
                        depthDesc.stencilClearValue = toWgpuStencilClear(framebufferInfo.stencilAttachment->clearValue);
                    }
                }
            }

            WGPURenderPassDescriptor passDesc {};
            passDesc.colorAttachmentCount   = 1;
            passDesc.colorAttachments       = &colorDesc;
            passDesc.depthStencilAttachment = m_DepthView != nullptr ? &depthDesc : nullptr;

#if defined(__EMSCRIPTEN__)
            WGPUPassTimestampWrites timestampWrites {};
#else
            WGPURenderPassTimestampWrites timestampWrites {};
#endif
            if (m_Backend)
            {
                WGPUQuerySet querySet {nullptr};
                uint32_t     beginIndex {0};
                uint32_t     endIndex {0};
                if (m_Backend->consumePassTimestampWriteRequest(querySet, beginIndex, endIndex))
                {
                    timestampWrites.querySet                  = querySet;
                    timestampWrites.beginningOfPassWriteIndex = beginIndex;
                    timestampWrites.endOfPassWriteIndex       = endIndex;
                    passDesc.timestampWrites                  = &timestampWrites;
                }
            }

            m_RenderPass = wgpuCommandEncoderBeginRenderPass(m_Encoder, &passDesc);
            if (m_RenderPass == nullptr)
            {
                throw std::runtime_error("WebGPUCommandBuffer beginRendering failed: cannot begin render pass");
            }

#endif
            m_SkipCurrentRendering       = false;
            m_InsideRendering            = true;
            m_PipelineBoundInCurrentPass = false;
            return *this;
        }

        WebGPUCommandBuffer& WebGPUCommandBuffer::endRendering()
        {
            if (!m_InsideRendering)
            {
                return *this;
            }
#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
            if (!m_SkipCurrentRendering && m_RenderPass != nullptr)
            {
                wgpuRenderPassEncoderEnd(m_RenderPass);
                if (m_Backend != nullptr)
                {
                    m_Backend->finalizePendingPassTimestampQueries(
                        m_Encoder ? reinterpret_cast<std::uintptr_t>(m_Encoder) : 0u);
                }
                wgpuRenderPassEncoderRelease(m_RenderPass);
                m_RenderPass = nullptr;
            }
            if (!m_SkipCurrentRendering && m_RenderView != nullptr)
            {
                if (m_OwnsRenderView)
                {
                    wgpuTextureViewRelease(m_RenderView);
                }
                m_RenderView     = nullptr;
                m_OwnsRenderView = false;
            }
            if (!m_SkipCurrentRendering && m_DepthView != nullptr)
            {
                if (m_OwnsDepthView)
                {
                    wgpuTextureViewRelease(m_DepthView);
                }
                m_DepthView     = nullptr;
                m_OwnsDepthView = false;
            }
#endif
            m_InsideRendering            = false;
            m_SkipCurrentRendering       = false;
            m_PipelineBoundInCurrentPass = false;
            m_PendingRenderBindGroups.fill(nullptr);
            return *this;
        }

        WebGPUCommandBuffer& WebGPUCommandBuffer::setViewport(const Rect2D&) { return *this; }
        WebGPUCommandBuffer& WebGPUCommandBuffer::setScissor(const Rect2D&) { return *this; }

        WebGPUCommandBuffer& WebGPUCommandBuffer::draw(const GeometryInfo& geometryInfo, const uint32_t numInstances)
        {
            if (m_SkipCurrentRendering)
            {
                return *this;
            }
#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
            if (!m_InsideRendering || m_RenderPass == nullptr)
            {
                throw std::runtime_error("WebGPUCommandBuffer draw failed: not inside render pass");
            }
            if (!m_PipelineBoundInCurrentPass && m_BoundPipeline != nullptr)
            {
                wgpuRenderPassEncoderSetPipeline(m_RenderPass, m_BoundPipeline);
                m_PipelineBoundInCurrentPass = true;
            }

            for (DescriptorSetIndex set = 0; set < kMinNumDescriptorSets; ++set)
            {
                if (m_PendingRenderBindGroups[set] != nullptr)
                {
                    wgpuRenderPassEncoderSetBindGroup(
                        m_RenderPass, set, m_PendingRenderBindGroups[set], 0, nullptr);
                }
            }

            if (m_BoundPipelineObject != nullptr && m_Backend != nullptr)
            {
                for (DescriptorSetIndex set = 0; set < kMinNumDescriptorSets; ++set)
                {
                    const auto key = m_BoundPipelineObject->getDescriptorSetLayout(set);
                    if (!key)
                    {
                        continue;
                    }
                    if (m_PendingRenderBindGroups[set] != nullptr)
                    {
                        continue;
                    }
                    const auto bindingsIt = m_Backend->m_DescriptorSetLayoutBindings.find(key.value);
                    if (bindingsIt == m_Backend->m_DescriptorSetLayoutBindings.end() || !bindingsIt->second.empty())
                    {
                        continue;
                    }

                    auto emptyIt = m_EmptyBindGroups.find(key.value);
                    if (emptyIt == m_EmptyBindGroups.end())
                    {
                        const auto layoutIt = m_Backend->m_DescriptorSetLayouts.find(key.value);
                        if (layoutIt == m_Backend->m_DescriptorSetLayouts.end())
                        {
                            continue;
                        }
                        WGPUBindGroupDescriptor emptyDesc {};
                        emptyDesc.layout           = layoutIt->second;
                        auto* const emptyBindGroup = wgpuDeviceCreateBindGroup(m_Device, &emptyDesc);
                        if (emptyBindGroup == nullptr)
                        {
                            continue;
                        }
                        emptyIt = m_EmptyBindGroups.emplace(key.value, emptyBindGroup).first;
                    }

                    wgpuRenderPassEncoderSetBindGroup(m_RenderPass, set, emptyIt->second, 0, nullptr);
                }
            }

            if (geometryInfo.vertexBuffer != nullptr && geometryInfo.vertexBuffer->getHandle() != 0)
            {
                auto* const vertexBufferHandle = reinterpret_cast<WGPUBuffer>(geometryInfo.vertexBuffer->getHandle());
                const uint64_t maxSize         = geometryInfo.vertexBuffer->getSize();
                wgpuRenderPassEncoderSetVertexBuffer(m_RenderPass, 0, vertexBufferHandle, 0u, maxSize);
            }

            if (geometryInfo.indexBuffer != nullptr && geometryInfo.indexBuffer->getHandle() != 0 &&
                geometryInfo.numIndices > 0)
            {
                auto* const    indexBufferHandle = reinterpret_cast<WGPUBuffer>(geometryInfo.indexBuffer->getHandle());
                const auto     indexFormat       = geometryInfo.indexBuffer->getIndexType() == IndexType::eUInt16 ?
                                                       WGPUIndexFormat_Uint16 :
                                                       WGPUIndexFormat_Uint32;
                const auto     indexStride       = geometryInfo.indexBuffer->getStride();
                const uint64_t byteOffset =
                    static_cast<uint64_t>(geometryInfo.indexOffset) * static_cast<uint64_t>(indexStride);
                const uint64_t maxSize = geometryInfo.indexBuffer->getSize();
                const uint64_t size    = byteOffset <= maxSize ? (maxSize - byteOffset) : 0u;
                wgpuRenderPassEncoderSetIndexBuffer(m_RenderPass, indexBufferHandle, indexFormat, byteOffset, size);
                wgpuRenderPassEncoderDrawIndexed(m_RenderPass,
                                                 geometryInfo.numIndices,
                                                 numInstances,
                                                 0u,
                                                 static_cast<int32_t>(geometryInfo.vertexOffset),
                                                 0u);
            }
            else
            {
                wgpuRenderPassEncoderDraw(
                    m_RenderPass, geometryInfo.numVertices, numInstances, geometryInfo.vertexOffset, 0u);
            }
#else
            (void)geometryInfo;
            (void)numInstances;
#endif
            return *this;
        }

        WebGPUCommandBuffer& WebGPUCommandBuffer::drawFullScreenTriangle() { return draw({.numVertices = 3u}, 1u); }
        WebGPUCommandBuffer& WebGPUCommandBuffer::drawCube() { unsupported("drawCube"); }
        WebGPUCommandBuffer& WebGPUCommandBuffer::drawIndirect(const DrawIndirectInfo& info)
        {
#if !defined(VULTRA_ENABLE_WEBGPU) || !VULTRA_ENABLE_WEBGPU
            (void)info;
            return *this;
#else
            if (!m_InsideRendering || m_RenderPass == nullptr || info.buffer == nullptr ||
                !static_cast<bool>(*info.buffer))
            {
                return *this;
            }
            if (!m_PipelineBoundInCurrentPass && m_BoundPipeline != nullptr)
            {
                wgpuRenderPassEncoderSetPipeline(m_RenderPass, m_BoundPipeline);
                m_PipelineBoundInCurrentPass = true;
            }
            for (DescriptorSetIndex set = 0; set < kMinNumDescriptorSets; ++set)
            {
                if (m_PendingRenderBindGroups[set] != nullptr)
                {
                    wgpuRenderPassEncoderSetBindGroup(
                        m_RenderPass, set, m_PendingRenderBindGroups[set], 0, nullptr);
                }
            }
            wgpuRenderPassEncoderDrawIndirect(m_RenderPass,
                                              reinterpret_cast<WGPUBuffer>(info.buffer->getHandle()),
                                              static_cast<uint64_t>(info.firstCommand) * info.buffer->getStride());
            return *this;
#endif
        }
        WebGPUCommandBuffer& WebGPUCommandBuffer::drawIndirectCount(const DrawIndirectInfo&, const Buffer&, uint32_t)
        {
            unsupported("drawIndirectCount");
        }
        WebGPUCommandBuffer& WebGPUCommandBuffer::drawMeshTask(const glm::uvec3&) { unsupported("drawMeshTask"); }

        WebGPUCommandBuffer& WebGPUCommandBuffer::clear(const Buffer& buffer, uint32_t value)
        {
#if !defined(VULTRA_ENABLE_WEBGPU) || !VULTRA_ENABLE_WEBGPU
            (void)buffer;
            (void)value;
            return *this;
#else
            if (m_Encoder == nullptr || !buffer)
            {
                return *this;
            }
            if (m_ComputePass != nullptr)
            {
                wgpuComputePassEncoderEnd(m_ComputePass);
                wgpuComputePassEncoderRelease(m_ComputePass);
                m_ComputePass = nullptr;
            }
            if (m_InsideRendering)
            {
                throw std::runtime_error("WebGPUCommandBuffer clear(Buffer) failed: inside render pass");
            }
            if (value == 0u)
            {
                wgpuCommandEncoderClearBuffer(
                    m_Encoder, reinterpret_cast<WGPUBuffer>(buffer.getHandle()), 0, buffer.getSize());
                return *this;
            }

            std::vector<uint32_t> data(
                static_cast<size_t>((buffer.getSize() + sizeof(uint32_t) - 1u) / sizeof(uint32_t)), value);
            wgpuQueueWriteBuffer(
                m_Queue, reinterpret_cast<WGPUBuffer>(buffer.getHandle()), 0, data.data(), buffer.getSize());
            return *this;
#endif
        }
        WebGPUCommandBuffer& WebGPUCommandBuffer::clear(Texture&, const ClearValue&) { unsupported("clear(Texture)"); }
        WebGPUCommandBuffer&
        WebGPUCommandBuffer::copyBuffer(const Buffer& src, Buffer& dst, const rhi::BufferCopy& region)
        {
            if (region.size == 0)
            {
                return *this;
            }

#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
            if (m_Encoder != nullptr && src.getHandle() != 0 && dst.getHandle() != 0)
            {
                wgpuCommandEncoderCopyBufferToBuffer(m_Encoder,
                                                     reinterpret_cast<WGPUBuffer>(src.getHandle()),
                                                     region.srcOffset,
                                                     reinterpret_cast<WGPUBuffer>(dst.getHandle()),
                                                     region.dstOffset,
                                                     region.size);
                return *this;
            }
#endif
            auto* srcPtr = static_cast<std::byte*>(const_cast<Buffer&>(src).map());
            auto* dstPtr = static_cast<std::byte*>(dst.map());
            std::memcpy(dstPtr + region.dstOffset, srcPtr + region.srcOffset, static_cast<size_t>(region.size));
            dst.unmap();
            return *this;
        }
        WebGPUCommandBuffer& WebGPUCommandBuffer::copyBuffer(const Buffer& src, Texture& dst)
        {
            const auto extent  = dst.getExtent();
            const auto regions = std::array {
                BufferImageCopy {
                    .aspectMask        = ImageAspectFlags::eColor,
                    .layerCount        = std::max(1u, dst.getLayerFaceCount()),
                    .imageExtentWidth  = extent.width,
                    .imageExtentHeight = extent.height,
                    .imageExtentDepth  = 1u,
                },
            };
            return copyBuffer(src, dst, regions);
        }
        WebGPUCommandBuffer&
        WebGPUCommandBuffer::copyBuffer(const Buffer& src, Texture& dst, std::span<const BufferImageCopy> regions)
        {
#if !defined(VULTRA_ENABLE_WEBGPU) || !VULTRA_ENABLE_WEBGPU
            (void)src;
            (void)dst;
            (void)regions;
            return *this;
#else
            if (m_Queue == nullptr || !src || !dst || TextureAccess::getImageHandle(dst) == 0 || regions.empty())
            {
                return *this;
            }

            const auto bytesPerPixel = getBytesPerPixel(dst.getPixelFormat());
            if (bytesPerPixel == 0)
            {
                unsupported("copyBuffer(Buffer,Texture,Regions unsupported pixel format)");
                return *this;
            }

            auto* srcData = static_cast<std::byte*>(const_cast<Buffer&>(src).map());
            if (srcData == nullptr)
            {
                return *this;
            }

            auto* const texture = reinterpret_cast<WGPUTexture>(TextureAccess::getImageHandle(dst));
            for (const auto& region : regions)
            {
                const auto width        = std::max(1u, region.imageExtentWidth);
                const auto height       = std::max(1u, region.imageExtentHeight);
                const auto rowLength    = region.bufferRowLength == 0 ? width : region.bufferRowLength;
                const auto rowsPerImage = region.bufferImageHeight == 0 ? height : region.bufferImageHeight;
                const auto depthOrLayers =
                    std::max(1u, region.layerCount > 1u ? region.layerCount : region.imageExtentDepth);
                const auto bytesPerRow = rowLength * bytesPerPixel;
                const auto dataSize    = static_cast<uint64_t>(bytesPerRow) * static_cast<uint64_t>(rowsPerImage) *
                                          static_cast<uint64_t>(depthOrLayers - 1u) +
                                      static_cast<uint64_t>(bytesPerRow) * static_cast<uint64_t>(height);

                WGPUTexelCopyTextureInfo dstCopy {};
                dstCopy.texture  = texture;
                dstCopy.mipLevel = region.mipLevel;
                dstCopy.origin.x = static_cast<uint32_t>(std::max(0, region.imageOffsetX));
                dstCopy.origin.y = static_cast<uint32_t>(std::max(0, region.imageOffsetY));
                dstCopy.origin.z = region.baseArrayLayer + static_cast<uint32_t>(std::max(0, region.imageOffsetZ));
                dstCopy.aspect   = webgpu::toWgpuTextureAspect(region.aspectMask);

                WGPUTexelCopyBufferLayout srcLayout {};
                srcLayout.offset       = region.bufferOffset;
                srcLayout.bytesPerRow  = bytesPerRow;
                srcLayout.rowsPerImage = rowsPerImage;

                WGPUExtent3D writeExtent {};
                writeExtent.width              = width;
                writeExtent.height             = height;
                writeExtent.depthOrArrayLayers = depthOrLayers;

                wgpuQueueWriteTexture(m_Queue, &dstCopy, srcData, dataSize, &srcLayout, &writeExtent);
            }

            return *this;
#endif
        }
        WebGPUCommandBuffer& WebGPUCommandBuffer::copyImage(const Texture&, const Buffer&, const rhi::ImageAspect)
        {
            unsupported("copyImage");
        }
        WebGPUCommandBuffer&
        WebGPUCommandBuffer::update(Buffer& dst, const uint64_t offset, const uint64_t size, const void* data)
        {
            if (size == 0 || data == nullptr)
            {
                return *this;
            }

#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
            auto fallbackToQueueWrite = [&]() {
                if (m_Queue != nullptr && dst.getHandle() != 0)
                {
                    wgpuQueueWriteBuffer(m_Queue, reinterpret_cast<WGPUBuffer>(dst.getHandle()), offset, data, size);
                    return true;
                }
                return false;
            };

            if (m_Device != nullptr && m_Encoder != nullptr && dst.getHandle() != 0 && (offset % 4u) == 0u &&
                (size % 4u) == 0u)
            {
                if (m_ComputePass != nullptr)
                {
                    wgpuComputePassEncoderEnd(m_ComputePass);
                    wgpuComputePassEncoderRelease(m_ComputePass);
                    m_ComputePass = nullptr;
                }

                WGPUBufferDescriptor stagingDesc {};
                stagingDesc.usage            = WGPUBufferUsage_CopySrc;
                stagingDesc.size             = size;
                stagingDesc.mappedAtCreation = true;
                auto* const stagingBuffer    = wgpuDeviceCreateBuffer(m_Device, &stagingDesc);
                if (stagingBuffer == nullptr)
                {
                    if (!fallbackToQueueWrite())
                    {
                        return *this;
                    }
                    return *this;
                }

                void* mapped = wgpuBufferGetMappedRange(stagingBuffer, 0u, size);
                if (mapped == nullptr)
                {
                    wgpuBufferRelease(stagingBuffer);
                    if (!fallbackToQueueWrite())
                    {
                        return *this;
                    }
                    return *this;
                }

                std::memcpy(mapped, data, static_cast<size_t>(size));
                wgpuBufferUnmap(stagingBuffer);

                wgpuCommandEncoderCopyBufferToBuffer(
                    m_Encoder, stagingBuffer, 0u, reinterpret_cast<WGPUBuffer>(dst.getHandle()), offset, size);
                m_TransientUploadBuffers.push_back(stagingBuffer);
                return *this;
            }

            if (fallbackToQueueWrite())
            {
                return *this;
            }
#endif
            auto* dstPtr = static_cast<std::byte*>(dst.map());
            std::memcpy(dstPtr + offset, data, static_cast<size_t>(size));
            dst.unmap();
            return *this;
        }
        WebGPUCommandBuffer& WebGPUCommandBuffer::blit(Texture&, Texture&, TexelFilter, uint32_t, uint32_t)
        {
            unsupported("blit");
        }
        WebGPUCommandBuffer& WebGPUCommandBuffer::generateMipmaps(Texture&, TexelFilter)
        {
            unsupported("generateMipmaps");
        }

        WebGPUCommandBuffer& WebGPUCommandBuffer::flushBarriers()
        {
            m_BarrierBuilder = Barrier::Builder {};
            return *this;
        }

        void WebGPUCommandBuffer::pushDebugGroup(const std::string_view label) const
        {
            (void)label;
        }

        void WebGPUCommandBuffer::popDebugGroup() const
        {
        }

        [[noreturn]] void WebGPUCommandBuffer::unsupported(const char* name)
        {
            throw std::runtime_error(std::string("WebGPUCommandBuffer operation is not implemented: ") + name);
        }

        void WebGPUCommandBuffer::releaseTransientResources() noexcept
        {
#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
            if (m_RenderPass != nullptr)
            {
                wgpuRenderPassEncoderRelease(m_RenderPass);
                m_RenderPass = nullptr;
            }
            if (m_ComputePass != nullptr)
            {
                wgpuComputePassEncoderRelease(m_ComputePass);
                m_ComputePass = nullptr;
            }
            if (m_RenderView != nullptr)
            {
                if (m_OwnsRenderView)
                {
                    wgpuTextureViewRelease(m_RenderView);
                }
                m_RenderView     = nullptr;
                m_OwnsRenderView = false;
            }
            if (m_DepthView != nullptr)
            {
                if (m_OwnsDepthView)
                {
                    wgpuTextureViewRelease(m_DepthView);
                }
                m_DepthView     = nullptr;
                m_OwnsDepthView = false;
            }
            if (m_Encoder != nullptr)
            {
                wgpuCommandEncoderRelease(m_Encoder);
                m_Encoder = nullptr;
            }
            for (auto& [_, bindGroup] : m_EmptyBindGroups)
            {
                if (bindGroup != nullptr)
                {
                    wgpuBindGroupRelease(bindGroup);
                }
            }
#endif
            m_EmptyBindGroups.clear();
            m_PendingComputeBindGroups.fill(nullptr);
            for (auto* buffer : m_TransientUploadBuffers)
            {
                if (buffer != nullptr)
                {
                    wgpuBufferRelease(buffer);
                }
            }
            m_TransientUploadBuffers.clear();
            for (auto& [_, bindGroup] : m_PushConstantBindGroups)
            {
                if (bindGroup != nullptr)
                {
                    wgpuBindGroupRelease(bindGroup);
                }
            }
            m_PushConstantBindGroups.clear();
            if (m_PushConstantBuffer != nullptr)
            {
                wgpuBufferRelease(m_PushConstantBuffer);
                m_PushConstantBuffer = nullptr;
            }
            m_PushConstantBufferSize = 0;
            m_DescriptorSets.clear();
        }
    } // namespace rhi
} // namespace vultra
