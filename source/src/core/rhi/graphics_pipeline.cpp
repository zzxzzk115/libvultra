#include "vultra/core/rhi/graphics_pipeline.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/core/rhi/shader_module.hpp"
#include "vultra/core/rhi/shader_reflection.hpp"
#include "vultra/core/rhi/vk/macro.hpp"

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
                }
                assert(false);
                return static_cast<vk::ShaderStageFlagBits>(0);
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

            [[nodiscard]] auto toVk(const StencilOpState& desc)
            {
                return vk::StencilOpState {
                    static_cast<vk::StencilOp>(desc.failOp),
                    static_cast<vk::StencilOp>(desc.passOp),
                    static_cast<vk::StencilOp>(desc.depthFailOp),
                    static_cast<vk::CompareOp>(desc.compareOp),
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
                    return static_cast<vk::Format>(format);
                });
                return out;
            }

        } // namespace

        GraphicsPipeline::Builder::Builder()
        {
            constexpr auto kMaxNumStages = 3; // CS or VS/GS/FS
            m_ShaderStages.reserve(kMaxNumStages);

            m_DepthStencilState = {
                .sType             = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
                .depthTestEnable   = false,
                .depthWriteEnable  = true,
                .depthCompareOp    = VK_COMPARE_OP_LESS_OR_EQUAL,
                .stencilTestEnable = false,
                .minDepthBounds    = 0.0f,
                .maxDepthBounds    = 1.0f,
            };
            m_RasterizerState = {
                .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                .depthClampEnable        = false,
                .rasterizerDiscardEnable = false,
                .polygonMode             = VK_POLYGON_MODE_FILL,
                .cullMode                = VK_CULL_MODE_NONE,
                .frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE,
                .depthBiasEnable         = false,
                .lineWidth               = 1.0f,
            };
        }

        GraphicsPipeline::Builder& GraphicsPipeline::Builder::setDepthFormat(const PixelFormat depthFormat)
        {
            const auto aspectMask = getAspectMask(depthFormat);
            m_DepthFormat         = static_cast<bool>(aspectMask & vk::ImageAspectFlagBits::eDepth) ?
                                        static_cast<vk::Format>(depthFormat) :
                                        vk::Format::eUndefined;
            m_StencilFormat       = static_cast<bool>(aspectMask & vk::ImageAspectFlagBits::eStencil) ?
                                        static_cast<vk::Format>(depthFormat) :
                                        vk::Format::eUndefined;
            return *this;
        }

        GraphicsPipeline::Builder& GraphicsPipeline::Builder::setDepthBias(const DepthBias& desc)
        {
            m_RasterizerState.depthBiasEnable         = VK_TRUE;
            m_RasterizerState.depthBiasConstantFactor = desc.constantFactor;
            m_RasterizerState.depthBiasSlopeFactor    = desc.slopeFactor;
            return *this;
        }

        GraphicsPipeline::Builder&
        GraphicsPipeline::Builder::setColorFormats(std::initializer_list<PixelFormat> formats)
        {
            m_ColorAttachmentFormats = convert(formats);
            return *this;
        }

        GraphicsPipeline::Builder& GraphicsPipeline::Builder::setColorFormats(std::span<const PixelFormat> formats)
        {
            m_ColorAttachmentFormats = convert(formats);
            return *this;
        }

        GraphicsPipeline::Builder& GraphicsPipeline::Builder::setInputAssembly(const VertexAttributes& vertexAttributes)
        {
            m_VertexInputAttributes.clear();

            if (!vertexAttributes.empty())
            {
                m_VertexInputAttributes.reserve(vertexAttributes.size());

                uint32_t stride {0};
                for (const auto& [location, attrib] : vertexAttributes)
                {
                    if (attrib.offset != kIgnoreVertexAttribute)
                    {
                        m_VertexInputAttributes.push_back(vk::VertexInputAttributeDescription {
                            location,
                            0,
                            static_cast<vk::Format>(attrib.type),
                            attrib.offset,
                        });
                    }
                    stride += getSize(attrib.type);
                }
                m_VertexInput = {
                    .binding   = 0,
                    .stride    = stride,
                    .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
                };
            }
            else
            {
                m_VertexInput = kIgnoreVertexInput;
            }
            return *this;
        }

        GraphicsPipeline::Builder& GraphicsPipeline::Builder::setTopology(const PrimitiveTopology topology)
        {
            m_PrimitiveTopology = static_cast<vk::PrimitiveTopology>(topology);
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
            // Again, Sonarlint is wrong, DO NOT replace 'emplace' with 'try_emplace'.
            // A builder might be used to create multiple pipelines, hence it's
            // necessary to REPLACE a given shader with another one (try_emplace prevents
            // that).
            m_ShaderStages.emplace(type, stageInfo);
            return *this;
        }

        GraphicsPipeline::Builder& GraphicsPipeline::Builder::setDepthStencil(const DepthStencilState& desc)
        {
            m_DepthStencilState.depthTestEnable  = desc.depthTest;
            m_DepthStencilState.depthWriteEnable = desc.depthWrite;
            m_DepthStencilState.depthCompareOp   = static_cast<vk::CompareOp>(desc.depthCompareOp);

            m_DepthStencilState.stencilTestEnable = desc.stencilTestEnable;
            m_DepthStencilState.front             = toVk(desc.front);
            m_DepthStencilState.back              = toVk(desc.back.value_or(desc.front));
            return *this;
        }

        GraphicsPipeline::Builder& GraphicsPipeline::Builder::setRasterizer(const RasterizerState& desc)
        {
            m_RasterizerState.depthClampEnable = desc.depthClampEnable;
            m_RasterizerState.polygonMode      = static_cast<vk::PolygonMode>(desc.polygonMode);
            m_RasterizerState.cullMode         = static_cast<vk::CullModeFlagBits>(desc.cullMode);
            if (desc.depthBias)
            {
                m_RasterizerState.depthBiasEnable         = true;
                m_RasterizerState.depthBiasConstantFactor = desc.depthBias->constantFactor;
                m_RasterizerState.depthBiasSlopeFactor    = desc.depthBias->slopeFactor;
            }
            m_RasterizerState.lineWidth = desc.lineWidth;
            return *this;
        }

        GraphicsPipeline::Builder& GraphicsPipeline::Builder::setBlending(const AttachmentIndex index,
                                                                          const BlendState&     desc)
        {
            if (index >= m_BlendStates.size())
                m_BlendStates.resize(index + 1);

            m_BlendStates[index] = VkPipelineColorBlendAttachmentState {
                .blendEnable         = desc.enabled,
                .srcColorBlendFactor = static_cast<VkBlendFactor>(desc.srcColor),
                .dstColorBlendFactor = static_cast<VkBlendFactor>(desc.dstColor),
                .colorBlendOp        = static_cast<VkBlendOp>(desc.colorOp),
                .srcAlphaBlendFactor = static_cast<VkBlendFactor>(desc.srcAlpha),
                .dstAlphaBlendFactor = static_cast<VkBlendFactor>(desc.dstAlpha),
                .alphaBlendOp        = static_cast<VkBlendOp>(desc.alphaOp),
                .colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                                  VK_COLOR_COMPONENT_A_BIT};
            return *this;
        }

        GraphicsPipeline::Builder&
        GraphicsPipeline::Builder::setDynamicState(const std::initializer_list<vk::DynamicState> dynamicStates)
        {
            m_DynamicStates = dynamicStates;
            return *this;
        }

        GraphicsPipeline GraphicsPipeline::Builder::build(RenderDevice& rd)
        {
            // -- Dynamic rendering:

            vk::PipelineRenderingCreateInfoKHR renderingInfo {};
            renderingInfo.colorAttachmentCount    = static_cast<uint32_t>(m_ColorAttachmentFormats.size());
            renderingInfo.pColorAttachmentFormats = m_ColorAttachmentFormats.data();
            renderingInfo.depthAttachmentFormat   = m_DepthFormat;
            renderingInfo.stencilAttachmentFormat = m_StencilFormat;

            // -- Vertex Attributes:

            vk::PipelineVertexInputStateCreateInfo vertexInputStateInfo {};
            vertexInputStateInfo.vertexBindingDescriptionCount = m_VertexInput.stride > 0 ? 1u : 0u;
            vertexInputStateInfo.pVertexBindingDescriptions    = &m_VertexInput;
            vertexInputStateInfo.vertexAttributeDescriptionCount =
                static_cast<uint32_t>(m_VertexInputAttributes.size());
            vertexInputStateInfo.pVertexAttributeDescriptions = m_VertexInputAttributes.data();

            vk::PipelineInputAssemblyStateCreateInfo inputAssemblyInfo {};
            inputAssemblyInfo.topology               = m_PrimitiveTopology;
            inputAssemblyInfo.primitiveRestartEnable = m_PrimitiveTopology == vk::PrimitiveTopology::eTriangleStrip;

            // --

            const auto lineWidthRange   = rd.getDeviceLimits().lineWidthRange;
            m_RasterizerState.lineWidth = glm::clamp(m_RasterizerState.lineWidth, lineWidthRange[0], lineWidthRange[1]);

            // -- Shader stages:

            const auto numShaderStages = m_ShaderStages.size();
            assert(numShaderStages > 0);

            auto reflection = m_PipelineLayout ? std::nullopt : std::make_optional<ShaderReflection>();

            std::vector<ShaderModule> shaderModules; // For delayed destruction only.
            shaderModules.reserve(numShaderStages);
            std::vector<vk::PipelineShaderStageCreateInfo> shaderStages;
            shaderStages.reserve(numShaderStages);
            for (const auto& [shaderType, shaderStageInfo] : m_ShaderStages)
            {
                auto shaderModule = rd.createShaderModule(shaderType,
                                                          shaderStageInfo.code,
                                                          shaderStageInfo.entryPointName,
                                                          shaderStageInfo.defines,
                                                          reflection ? std::addressof(reflection.value()) : nullptr);
                if (!shaderModule)
                    continue;

                vk::PipelineShaderStageCreateInfo shaderStageCreateInfo {};
                shaderStageCreateInfo.stage  = toVk(shaderType);
                shaderStageCreateInfo.module = vk::ShaderModule {shaderModule};
                shaderStageCreateInfo.pName  = shaderStageInfo.entryPointName.data();

                shaderStages.push_back(shaderStageCreateInfo);
                shaderModules.emplace_back(std::move(shaderModule));
            }
            if (shaderStages.size() != numShaderStages)
                return {};

            if (reflection.has_value())
                m_PipelineLayout = reflectPipelineLayout(rd, *reflection);
            assert(m_PipelineLayout);

            // -- Blending state:

            assert(m_BlendStates.size() == m_ColorAttachmentFormats.size());

            vk::PipelineColorBlendStateCreateInfo colorBlendInfo {};
            colorBlendInfo.logicOpEnable   = false;
            colorBlendInfo.logicOp         = vk::LogicOp::eClear;
            colorBlendInfo.attachmentCount = static_cast<uint32_t>(m_BlendStates.size());
            colorBlendInfo.pAttachments    = m_BlendStates.data();

            // -- Dynamic state:

            vk::PipelineDynamicStateCreateInfo dynamicStateInfo {};
            dynamicStateInfo.dynamicStateCount = static_cast<uint32_t>(m_DynamicStates.size());
            dynamicStateInfo.pDynamicStates    = m_DynamicStates.data();

            // -- Assemble:

            vk::GraphicsPipelineCreateInfo graphicsPipelineInfo {};
            graphicsPipelineInfo.pNext               = &renderingInfo;
            graphicsPipelineInfo.stageCount          = static_cast<uint32_t>(shaderStages.size());
            graphicsPipelineInfo.pStages             = shaderStages.data();
            graphicsPipelineInfo.pVertexInputState   = &vertexInputStateInfo;
            graphicsPipelineInfo.pInputAssemblyState = &inputAssemblyInfo;
            graphicsPipelineInfo.pViewportState      = &kIgnoreViewportState;
            graphicsPipelineInfo.pRasterizationState = &m_RasterizerState;
            graphicsPipelineInfo.pMultisampleState   = &kIgnoreMultisampleState;
            graphicsPipelineInfo.pDepthStencilState  = &m_DepthStencilState;
            graphicsPipelineInfo.pColorBlendState    = &colorBlendInfo;
            graphicsPipelineInfo.pDynamicState       = &dynamicStateInfo;
            graphicsPipelineInfo.layout              = m_PipelineLayout.getHandle();
            graphicsPipelineInfo.renderPass          = nullptr;
            graphicsPipelineInfo.subpass = 0, graphicsPipelineInfo.basePipelineHandle = nullptr;

            const auto device = rd.m_Device;

            vk::Pipeline handle {nullptr};

            VK_CHECK(device.createGraphicsPipelines(rd.m_PipelineCache, 1, &graphicsPipelineInfo, nullptr, &handle),
                     "GraphicsPipeline",
                     "Failed to create graphics pipeline!");

            return GraphicsPipeline {device, std::move(m_PipelineLayout), handle};
        }

        GraphicsPipeline::GraphicsPipeline(const vk::Device   device,
                                           PipelineLayout&&   pipelineLayout,
                                           const vk::Pipeline pipeline) :
            BasePipeline {device, std::move(pipelineLayout), pipeline}
        {}
    } // namespace rhi
} // namespace vultra