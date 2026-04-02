#include "vultra/core/rhi/graphics_pipeline.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/core/rhi/shader_module.hpp"
#include "vultra/core/rhi/shader_reflection.hpp"
#include "vultra/core/rhi/backends/vk/conversions.hpp"
#include "vultra/core/rhi/backends/vk/macro.hpp"
#include "vultra/core/rhi/backends/vk/vulkan_pipeline_backend.hpp"
#include "vultra/core/rhi/backends/vk/vulkan_render_device_access.hpp"

#include <glm/common.hpp>

namespace vultra
{
    namespace rhi
    {
        namespace
        {
            [[nodiscard]] constexpr vk::ShaderStageFlagBits toVk(const ShaderType shaderType)
            {
                switch (shaderType)
                {
                    using enum ShaderType;

                    case eVertex:
                        return vk::ShaderStageFlagBits::eVertex;
                    case eGeometry:
                        return vk::ShaderStageFlagBits::eGeometry;
                    case eFragment:
                        return vk::ShaderStageFlagBits::eFragment;

                    case eCompute:
                        return vk::ShaderStageFlagBits::eCompute;

                    case eMesh:
                        return vk::ShaderStageFlagBits::eMeshEXT;

                    case eTask:
                        return vk::ShaderStageFlagBits::eTaskEXT;

                    case ShaderType::eRayGen:
                    case ShaderType::eMiss:
                    case ShaderType::eClosestHit:
                    case ShaderType::eAnyHit:
                    case ShaderType::eIntersect:
                        break;
                }
                assert(false);
                return static_cast<vk::ShaderStageFlagBits>(0);
            }

            [[nodiscard]] constexpr vk::StencilOp toVk(const StencilOp op)
            {
                switch (op)
                {
                    case StencilOp::eKeep:
                        return vk::StencilOp::eKeep;
                    case StencilOp::eZero:
                        return vk::StencilOp::eZero;
                    case StencilOp::eReplace:
                        return vk::StencilOp::eReplace;
                    case StencilOp::eIncrementAndClamp:
                        return vk::StencilOp::eIncrementAndClamp;
                    case StencilOp::eDecrementAndClamp:
                        return vk::StencilOp::eDecrementAndClamp;
                    case StencilOp::eInvert:
                        return vk::StencilOp::eInvert;
                    case StencilOp::eIncrementAndWrap:
                        return vk::StencilOp::eIncrementAndWrap;
                    case StencilOp::eDecrementAndWrap:
                        return vk::StencilOp::eDecrementAndWrap;
                }
                assert(false);
                return vk::StencilOp::eKeep;
            }

            [[nodiscard]] constexpr vk::PolygonMode toVk(const PolygonMode mode)
            {
                switch (mode)
                {
                    case PolygonMode::eFill:
                        return vk::PolygonMode::eFill;
                    case PolygonMode::eLine:
                        return vk::PolygonMode::eLine;
                    case PolygonMode::ePoint:
                        return vk::PolygonMode::ePoint;
                }
                assert(false);
                return vk::PolygonMode::eFill;
            }

            [[nodiscard]] constexpr vk::BlendOp toVk(const BlendOp op)
            {
                switch (op)
                {
                    case BlendOp::eAdd:
                        return vk::BlendOp::eAdd;
                    case BlendOp::eSubtract:
                        return vk::BlendOp::eSubtract;
                    case BlendOp::eReverseSubtract:
                        return vk::BlendOp::eReverseSubtract;
                    case BlendOp::eMin:
                        return vk::BlendOp::eMin;
                    case BlendOp::eMax:
                        return vk::BlendOp::eMax;
                }
                assert(false);
                return vk::BlendOp::eAdd;
            }

            [[nodiscard]] constexpr vk::BlendFactor toVk(const BlendFactor factor)
            {
                switch (factor)
                {
                    case BlendFactor::eZero:
                        return vk::BlendFactor::eZero;
                    case BlendFactor::eOne:
                        return vk::BlendFactor::eOne;
                    case BlendFactor::eSrcColor:
                        return vk::BlendFactor::eSrcColor;
                    case BlendFactor::eOneMinusSrcColor:
                        return vk::BlendFactor::eOneMinusSrcColor;
                    case BlendFactor::eDstColor:
                        return vk::BlendFactor::eDstColor;
                    case BlendFactor::eOneMinusDstColor:
                        return vk::BlendFactor::eOneMinusDstColor;
                    case BlendFactor::eSrcAlpha:
                        return vk::BlendFactor::eSrcAlpha;
                    case BlendFactor::eOneMinusSrcAlpha:
                        return vk::BlendFactor::eOneMinusSrcAlpha;
                    case BlendFactor::eDstAlpha:
                        return vk::BlendFactor::eDstAlpha;
                    case BlendFactor::eOneMinusDstAlpha:
                        return vk::BlendFactor::eOneMinusDstAlpha;
                    case BlendFactor::eConstantColor:
                        return vk::BlendFactor::eConstantColor;
                    case BlendFactor::eOneMinusConstantColor:
                        return vk::BlendFactor::eOneMinusConstantColor;
                    case BlendFactor::eConstantAlpha:
                        return vk::BlendFactor::eConstantAlpha;
                    case BlendFactor::eOneMinusConstantAlpha:
                        return vk::BlendFactor::eOneMinusConstantAlpha;
                    case BlendFactor::eSrcAlphaSaturate:
                        return vk::BlendFactor::eSrcAlphaSaturate;
                    case BlendFactor::eSrc1Color:
                        return vk::BlendFactor::eSrc1Color;
                    case BlendFactor::eOneMinusSrc1Color:
                        return vk::BlendFactor::eOneMinusSrc1Color;
                    case BlendFactor::eSrc1Alpha:
                        return vk::BlendFactor::eSrc1Alpha;
                    case BlendFactor::eOneMinusSrc1Alpha:
                        return vk::BlendFactor::eOneMinusSrc1Alpha;
                }
                assert(false);
                return vk::BlendFactor::eOne;
            }

            constexpr vk::VertexInputBindingDescription kIgnoreVertexInput {
                0,
                0,
                vk::VertexInputRate::eVertex,
            };

            constexpr vk::Viewport kNoViewport {
                0.0f,
                0.0f,
                1.0f,
                1.0f,
                0.0f,
                1.0f,
            };
            constexpr vk::Rect2D kNoScissor {
                vk::Offset2D {0, 0},
                vk::Extent2D {1, 1},
            };

            constexpr vk::PipelineViewportStateCreateInfo kIgnoreViewportState {
                {},
                1,
                &kNoViewport,
                1,
                &kNoScissor,
            };

            constexpr vk::PipelineMultisampleStateCreateInfo kIgnoreMultisampleState {
                {},
                vk::SampleCountFlagBits::e1,
                false,
            };

            [[nodiscard]] constexpr vk::PrimitiveTopology toVk(const PrimitiveTopology topology)
            {
                switch (topology)
                {
                    case PrimitiveTopology::eTriangleList:
                        return vk::PrimitiveTopology::eTriangleList;
                    case PrimitiveTopology::eTriangleStrip:
                        return vk::PrimitiveTopology::eTriangleStrip;
                    case PrimitiveTopology::eLineList:
                        return vk::PrimitiveTopology::eLineList;
                    case PrimitiveTopology::eLineStrip:
                        return vk::PrimitiveTopology::eLineStrip;
                    case PrimitiveTopology::ePointList:
                        return vk::PrimitiveTopology::ePointList;
                }

                assert(false);
                return vk::PrimitiveTopology::eTriangleList;
            }

            [[nodiscard]] constexpr vk::DynamicState toVk(const DynamicState state)
            {
                switch (state)
                {
                    case DynamicState::eViewport:
                        return vk::DynamicState::eViewport;
                    case DynamicState::eScissor:
                        return vk::DynamicState::eScissor;
                }
                return vk::DynamicState::eViewport;
            }

            [[nodiscard]] auto toVk(const StencilOpState& desc)
            {
                return vk::StencilOpState {
                    toVk(desc.failOp),
                    toVk(desc.passOp),
                    toVk(desc.depthFailOp),
                    toVk(desc.compareOp),
                    desc.compareMask,
                    desc.writeMask,
                    desc.reference,
                };
            }

        [[nodiscard]] auto convert(const auto& container)
        {
            std::vector<vk::Format> out(container.size());
            std::ranges::transform(container, out.begin(), [](const PixelFormat format) {
                assert(getAspectMask(format) & vk::ImageAspectFlagBits::eColor);
                return toVk(format);
            });
            return out;
        }

        [[nodiscard]] constexpr vk::Format toVk(const VertexAttribute::Type type)
        {
            switch (type)
            {
                case VertexAttribute::Type::eFloat:
                    return vk::Format::eR32Sfloat;
                case VertexAttribute::Type::eFloat2:
                    return vk::Format::eR32G32Sfloat;
                case VertexAttribute::Type::eFloat3:
                    return vk::Format::eR32G32B32Sfloat;
                case VertexAttribute::Type::eFloat4:
                    return vk::Format::eR32G32B32A32Sfloat;
                case VertexAttribute::Type::eInt4:
                    return vk::Format::eR32G32B32A32Sint;
                case VertexAttribute::Type::eUByte4_Norm:
                    return vk::Format::eR8G8B8A8Unorm;
            }
            return vk::Format::eUndefined;
        }

        } // namespace

        GraphicsPipeline::Builder::Builder()
        {
            constexpr auto kMaxNumStages = 3; // CS or VS/GS/FS
            m_ShaderStages.reserve(kMaxNumStages);
        }

        GraphicsPipeline::Builder::~Builder() = default;

        GraphicsPipeline::Builder& GraphicsPipeline::Builder::setDepthFormat(const PixelFormat depthFormat)
        {
            const auto aspectMask = getAspectMask(depthFormat);
            m_DepthFormat         = HasFlagValues(aspectMask, ImageAspectFlags::eDepth) ? depthFormat : PixelFormat::eUndefined;
            m_StencilFormat       = HasFlagValues(aspectMask, ImageAspectFlags::eStencil) ? depthFormat : PixelFormat::eUndefined;
            return *this;
        }

        GraphicsPipeline::Builder& GraphicsPipeline::Builder::setDepthBias(const DepthBias& desc)
        {
            m_RasterizerState.depthBias = desc;
            return *this;
        }

        GraphicsPipeline::Builder&
        GraphicsPipeline::Builder::setColorFormats(std::initializer_list<PixelFormat> formats)
        {
            m_ColorAttachmentFormats.assign(formats.begin(), formats.end());
            return *this;
        }

        GraphicsPipeline::Builder& GraphicsPipeline::Builder::setColorFormats(std::span<const PixelFormat> formats)
        {
            m_ColorAttachmentFormats.assign(formats.begin(), formats.end());
            return *this;
        }

        GraphicsPipeline::Builder& GraphicsPipeline::Builder::setViewMask(const uint32_t viewMask)
        {
            m_ViewMask = viewMask;
            return *this;
        }

        GraphicsPipeline::Builder& GraphicsPipeline::Builder::setInputAssembly(const VertexAttributes& vertexAttributes)
        {
            m_VertexAttributes = vertexAttributes;
            return *this;
        }

        GraphicsPipeline::Builder& GraphicsPipeline::Builder::setTopology(const PrimitiveTopology topology)
        {
            m_PrimitiveTopology = topology;
            return *this;
        }

        GraphicsPipeline::Builder& GraphicsPipeline::Builder::setPipelineLayout(PipelineLayout pipelineLayout)
        {
            m_PipelineLayout = std::move(pipelineLayout);
            return *this;
        }

        GraphicsPipeline::Builder& GraphicsPipeline::Builder::addShader(const ShaderType       type,
                                                                        const ShaderStageInfo& stageInfo)
        {
            m_ShaderStages.emplace(type, stageInfo);
            return *this;
        }

        GraphicsPipeline::Builder& GraphicsPipeline::Builder::addBuiltinShader(const ShaderType type, const SPIRV& spv)
        {
            m_BuiltinShaderStages.emplace(type, spv);
            return *this;
        }

        GraphicsPipeline::Builder& GraphicsPipeline::Builder::setDepthStencil(const DepthStencilState& desc)
        {
            m_DepthStencilState = desc;
            return *this;
        }

        GraphicsPipeline::Builder& GraphicsPipeline::Builder::setRasterizer(const RasterizerState& desc)
        {
            m_RasterizerState = desc;
            return *this;
        }

        GraphicsPipeline::Builder& GraphicsPipeline::Builder::setBlending(const AttachmentIndex index,
                                                                          const BlendState&     desc)
        {
            if (index >= m_BlendStates.size())
                m_BlendStates.resize(index + 1);
            m_BlendStates[index] = desc;
            return *this;
        }

        GraphicsPipeline::Builder&
        GraphicsPipeline::Builder::setDynamicState(const std::initializer_list<DynamicState> dynamicStates)
        {
            m_DynamicStates.assign(dynamicStates.begin(), dynamicStates.end());
            return *this;
        }

        GraphicsPipeline GraphicsPipeline::Builder::build(RenderDevice& rd)
        {
            const auto vkColorAttachmentFormats = convert(m_ColorAttachmentFormats);
            // -- Dynamic rendering:

            vk::PipelineRenderingCreateInfoKHR renderingInfo {};
            renderingInfo.viewMask                = m_ViewMask;
            renderingInfo.colorAttachmentCount    = static_cast<uint32_t>(vkColorAttachmentFormats.size());
            renderingInfo.pColorAttachmentFormats = vkColorAttachmentFormats.data();
            renderingInfo.depthAttachmentFormat   = m_DepthFormat != PixelFormat::eUndefined ? toVk(m_DepthFormat) : vk::Format::eUndefined;
            renderingInfo.stencilAttachmentFormat = m_StencilFormat != PixelFormat::eUndefined ? toVk(m_StencilFormat) : vk::Format::eUndefined;

            // -- Vertex Attributes:

            vk::VertexInputBindingDescription                vertexInput = kIgnoreVertexInput;
            std::vector<vk::VertexInputAttributeDescription> vertexInputAttributes;
            if (!m_VertexAttributes.empty())
            {
                vertexInputAttributes.reserve(m_VertexAttributes.size());
                uint32_t stride {0};
                for (const auto& [location, attrib] : m_VertexAttributes)
                {
                    if (attrib.offset != kIgnoreVertexAttribute)
                    {
                        vertexInputAttributes.push_back(vk::VertexInputAttributeDescription {
                            location,
                            0,
                            toVk(attrib.type),
                            attrib.offset,
                        });
                    }
                    stride += getSize(attrib.type);
                }
                vertexInput = {
                    .binding   = 0,
                    .stride    = stride,
                    .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
                };
            }

            vk::PipelineVertexInputStateCreateInfo vertexInputStateInfo {};
            vertexInputStateInfo.vertexBindingDescriptionCount = vertexInput.stride > 0 ? 1u : 0u;
            vertexInputStateInfo.pVertexBindingDescriptions    = &vertexInput;
            vertexInputStateInfo.vertexAttributeDescriptionCount =
                static_cast<uint32_t>(vertexInputAttributes.size());
            vertexInputStateInfo.pVertexAttributeDescriptions = vertexInputAttributes.data();

            vk::PipelineInputAssemblyStateCreateInfo inputAssemblyInfo {};
            inputAssemblyInfo.topology               = toVk(m_PrimitiveTopology);
            inputAssemblyInfo.primitiveRestartEnable = m_PrimitiveTopology == PrimitiveTopology::eTriangleStrip;

            // --

            const auto lineWidthRange   = rd.getLineWidthRange();
            auto rasterizerState = m_RasterizerState;
            rasterizerState.lineWidth = glm::clamp(rasterizerState.lineWidth, lineWidthRange[0], lineWidthRange[1]);

            // -- Shader stages:
            auto       reflection      = m_PipelineLayout ? std::nullopt : std::make_optional<ShaderReflection>();
            const auto numShaderStages = m_ShaderStages.size() + m_BuiltinShaderStages.size();
            assert(numShaderStages > 0);

            std::vector<ShaderModule> shaderModules; // For delayed reflection ownership only.
            shaderModules.reserve(numShaderStages);
            std::vector<vk::ShaderModule> shaderModuleHandles;
            shaderModuleHandles.reserve(numShaderStages);
            std::vector<vk::PipelineShaderStageCreateInfo> shaderStages;
            shaderStages.reserve(numShaderStages);

            // -- Builtin stages:
            for (const auto& [shaderType, spv] : m_BuiltinShaderStages)
            {
                auto shaderModule =
                    rd.createShaderModule(spv, reflection ? std::addressof(reflection.value()) : nullptr);
                if (!shaderModule)
                    continue;

                vk::ShaderModule shaderModuleHandle {nullptr};
                {
                    vk::ShaderModuleCreateInfo createInfo {};
                    createInfo.codeSize = sizeof(uint32_t) * shaderModule.getSpirv().size();
                    createInfo.pCode    = shaderModule.getSpirv().data();
                    const vk::Device device {reinterpret_cast<VkDevice>(VulkanRenderDeviceAccess::getDeviceHandle(rd))};
                    VK_CHECK(device.createShaderModule(&createInfo, nullptr, &shaderModuleHandle),
                             "GraphicsPipeline",
                             "Failed to create shader module");
                }

                vk::PipelineShaderStageCreateInfo shaderStageCreateInfo {};
                shaderStageCreateInfo.stage  = toVk(shaderType);
                shaderStageCreateInfo.module = shaderModuleHandle;
                shaderStageCreateInfo.pName  = "main";

                shaderStages.push_back(shaderStageCreateInfo);
                shaderModules.emplace_back(std::move(shaderModule));
                shaderModuleHandles.push_back(shaderModuleHandle);
            }

            // -- Shader stages:
            for (const auto& [shaderType, shaderStageInfo] : m_ShaderStages)
            {
                auto shaderModule = rd.createShaderModule(shaderType,
                                                          shaderStageInfo.code,
                                                          shaderStageInfo.entryPointName,
                                                          shaderStageInfo.defines,
                                                          reflection ? std::addressof(reflection.value()) : nullptr);
                if (!shaderModule)
                    continue;

                vk::ShaderModule shaderModuleHandle {nullptr};
                {
                    vk::ShaderModuleCreateInfo createInfo {};
                    createInfo.codeSize = sizeof(uint32_t) * shaderModule.getSpirv().size();
                    createInfo.pCode    = shaderModule.getSpirv().data();
                    const vk::Device device {reinterpret_cast<VkDevice>(VulkanRenderDeviceAccess::getDeviceHandle(rd))};
                    VK_CHECK(device.createShaderModule(&createInfo, nullptr, &shaderModuleHandle),
                             "GraphicsPipeline",
                             "Failed to create shader module");
                }

                vk::PipelineShaderStageCreateInfo shaderStageCreateInfo {};
                shaderStageCreateInfo.stage  = toVk(shaderType);
                shaderStageCreateInfo.module = shaderModuleHandle;
                shaderStageCreateInfo.pName  = shaderStageInfo.entryPointName.data();

                shaderStages.push_back(shaderStageCreateInfo);
                shaderModules.emplace_back(std::move(shaderModule));
                shaderModuleHandles.push_back(shaderModuleHandle);
            }
            if (shaderStages.size() != numShaderStages)
            {
                for (const auto shaderModuleHandle : shaderModuleHandles)
                {
                    const vk::Device device {reinterpret_cast<VkDevice>(VulkanRenderDeviceAccess::getDeviceHandle(rd))};
                    device.destroyShaderModule(shaderModuleHandle);
                }
                return {};
            }

            if (reflection.has_value())
                m_PipelineLayout = reflectPipelineLayout(rd, *reflection);
            assert(m_PipelineLayout);

            // -- Blending state:

            assert(m_BlendStates.size() == m_ColorAttachmentFormats.size());

            std::vector<vk::PipelineColorBlendAttachmentState> vkBlendStates;
            vkBlendStates.reserve(m_BlendStates.size());
            for (const auto& blend : m_BlendStates)
            {
                auto vkBlendState = vk::PipelineColorBlendAttachmentState {};
                vkBlendState.blendEnable         = blend.enabled ? VK_TRUE : VK_FALSE;
                vkBlendState.srcColorBlendFactor = toVk(blend.srcColor);
                vkBlendState.dstColorBlendFactor = toVk(blend.dstColor);
                vkBlendState.colorBlendOp        = toVk(blend.colorOp);
                vkBlendState.srcAlphaBlendFactor = toVk(blend.srcAlpha);
                vkBlendState.dstAlphaBlendFactor = toVk(blend.dstAlpha);
                vkBlendState.alphaBlendOp        = toVk(blend.alphaOp);
                vkBlendState.colorWriteMask      = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                                              vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
                vkBlendStates.push_back(vkBlendState);
            }

            vk::PipelineColorBlendStateCreateInfo colorBlendInfo {};
            colorBlendInfo.logicOpEnable   = false;
            colorBlendInfo.logicOp         = vk::LogicOp::eClear;
            colorBlendInfo.attachmentCount = static_cast<uint32_t>(vkBlendStates.size());
            colorBlendInfo.pAttachments    = vkBlendStates.data();

            // -- Dynamic state:

            std::vector<vk::DynamicState> vkDynamicStates;
            vkDynamicStates.reserve(m_DynamicStates.size());
            for (const auto state : m_DynamicStates)
                vkDynamicStates.push_back(toVk(state));

            vk::PipelineRasterizationStateCreateInfo vkRasterizerState {};
            vkRasterizerState.sType                   = vk::StructureType::ePipelineRasterizationStateCreateInfo;
            vkRasterizerState.depthClampEnable        = rasterizerState.depthClampEnable;
            vkRasterizerState.rasterizerDiscardEnable = false;
            vkRasterizerState.polygonMode             = toVk(rasterizerState.polygonMode);
            vkRasterizerState.cullMode                = toVk(rasterizerState.cullMode);
            vkRasterizerState.frontFace               = vk::FrontFace::eCounterClockwise;
            vkRasterizerState.depthBiasEnable         = rasterizerState.depthBias.has_value();
            vkRasterizerState.depthBiasConstantFactor =
                rasterizerState.depthBias ? rasterizerState.depthBias->constantFactor : 0.0f;
            vkRasterizerState.depthBiasSlopeFactor =
                rasterizerState.depthBias ? rasterizerState.depthBias->slopeFactor : 0.0f;
            vkRasterizerState.lineWidth = rasterizerState.lineWidth;

            vk::PipelineDepthStencilStateCreateInfo vkDepthStencilState {};
            vkDepthStencilState.sType             = vk::StructureType::ePipelineDepthStencilStateCreateInfo;
            vkDepthStencilState.depthTestEnable   = m_DepthStencilState.depthTest;
            vkDepthStencilState.depthWriteEnable  = m_DepthStencilState.depthWrite;
            vkDepthStencilState.depthCompareOp    = toVk(m_DepthStencilState.depthCompareOp);
            vkDepthStencilState.stencilTestEnable = m_DepthStencilState.stencilTestEnable;
            vkDepthStencilState.front             = toVk(m_DepthStencilState.front);
            vkDepthStencilState.back              = toVk(m_DepthStencilState.back.value_or(m_DepthStencilState.front));
            vkDepthStencilState.minDepthBounds    = 0.0f;
            vkDepthStencilState.maxDepthBounds    = 1.0f;

            vk::PipelineDynamicStateCreateInfo dynamicStateInfo {};
            dynamicStateInfo.dynamicStateCount = static_cast<uint32_t>(vkDynamicStates.size());
            dynamicStateInfo.pDynamicStates    = vkDynamicStates.data();

            // -- Assemble:

            vk::GraphicsPipelineCreateInfo graphicsPipelineInfo {};
            graphicsPipelineInfo.pNext               = &renderingInfo;
            graphicsPipelineInfo.stageCount          = static_cast<uint32_t>(shaderStages.size());
            graphicsPipelineInfo.pStages             = shaderStages.data();
            graphicsPipelineInfo.pVertexInputState   = &vertexInputStateInfo;
            graphicsPipelineInfo.pInputAssemblyState = &inputAssemblyInfo;
            graphicsPipelineInfo.pViewportState      = &kIgnoreViewportState;
            graphicsPipelineInfo.pRasterizationState = &vkRasterizerState;
            graphicsPipelineInfo.pMultisampleState   = &kIgnoreMultisampleState;
            graphicsPipelineInfo.pDepthStencilState  = &vkDepthStencilState;
            graphicsPipelineInfo.pColorBlendState    = &colorBlendInfo;
            graphicsPipelineInfo.pDynamicState       = &dynamicStateInfo;
            graphicsPipelineInfo.layout =
                vk::PipelineLayout {reinterpret_cast<VkPipelineLayout>(m_PipelineLayout.getHandle())};
            graphicsPipelineInfo.renderPass          = nullptr;
            graphicsPipelineInfo.subpass = 0, graphicsPipelineInfo.basePipelineHandle = nullptr;

            const vk::Device device {reinterpret_cast<VkDevice>(VulkanRenderDeviceAccess::getDeviceHandle(rd))};

            vk::Pipeline handle {nullptr};
            const vk::PipelineCache pipelineCache {
                reinterpret_cast<VkPipelineCache>(VulkanRenderDeviceAccess::getPipelineCacheHandle(rd))};
            const auto result = device.createGraphicsPipelines(pipelineCache, 1, &graphicsPipelineInfo, nullptr, &handle);
            if (result != vk::Result::eSuccess)
            {
                VULTRA_CORE_ERROR("[GraphicsPipeline] Failed to create graphics pipeline: {}",
                                  vk::to_string(result));
                throw std::runtime_error("Failed to create graphics pipeline");
            }

            for (const auto shaderModuleHandle : shaderModuleHandles)
            {
                device.destroyShaderModule(shaderModuleHandle);
            }

            return GraphicsPipeline {std::move(m_PipelineLayout),
                                     reinterpret_cast<std::uintptr_t>(static_cast<VkPipeline>(handle)),
                                     std::make_unique<VulkanPipelineBackend>(VulkanRenderDeviceAccess::getDeviceHandle(rd))};
        }

        GraphicsPipeline::GraphicsPipeline(PipelineLayout&&                       pipelineLayout,
                                           const std::uintptr_t                   pipeline,
                                           std::unique_ptr<IPipelineBackend> destroyBackend) :
            BasePipeline {std::move(pipelineLayout), pipeline, std::move(destroyBackend)}
        {}
    } // namespace rhi
} // namespace vultra
