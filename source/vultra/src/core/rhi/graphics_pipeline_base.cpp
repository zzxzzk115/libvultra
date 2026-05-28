#include "vultra/core/rhi/graphics_pipeline.hpp"

#include "vultra/core/rhi/texture.hpp"

namespace vultra
{
    namespace rhi
    {
        GraphicsPipeline::Builder::Builder()
        {
            constexpr auto kMaxNumStages = 3; // CS or VS/GS/FS
            m_ShaderStages.reserve(kMaxNumStages);
        }

        GraphicsPipeline::Builder::~Builder() = default;

        GraphicsPipeline::Builder& GraphicsPipeline::Builder::setDepthFormat(const PixelFormat depthFormat)
        {
            const auto aspectMask = getAspectMask(depthFormat);
            m_DepthFormat = HasFlagValues(aspectMask, ImageAspectFlags::eDepth) ? depthFormat : PixelFormat::eUndefined;
            m_StencilFormat =
                HasFlagValues(aspectMask, ImageAspectFlags::eStencil) ? depthFormat : PixelFormat::eUndefined;
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

        GraphicsPipeline::Builder& GraphicsPipeline::Builder::setVertexStride(const uint32_t vertexStride)
        {
            m_VertexStride = vertexStride;
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

        GraphicsPipeline::Builder& GraphicsPipeline::Builder::addBuiltinShader(const ShaderType        type,
                                                                               const SPIRV&            spv,
                                                                               const ShaderReflection* reflection)
        {
            m_BuiltinShaderStages.emplace(type,
                                          BuiltinShaderStage {
                                              .spirv      = spv,
                                              .reflection = reflection ? std::make_optional(*reflection) : std::nullopt,
                                          });
            return *this;
        }

        GraphicsPipeline::Builder&
        GraphicsPipeline::Builder::addBuiltinShader(const ShaderType                          type,
                                                    const ShaderLibraryRuntime::LoadedShader& shader)
        {
            return addBuiltinShader(type, shader.spirv, &shader.reflection);
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

        GraphicsPipeline::GraphicsPipeline(PipelineLayout&&           pipelineLayout,
                                           const std::uintptr_t       pipeline,
                                           std::unique_ptr<IPipeline> destroyBackend,
                                           const PixelFormat          depthFormat,
                                           const PixelFormat          stencilFormat,
                                           const DepthStencilState    depthStencilState) :
            BasePipeline {std::move(pipelineLayout), pipeline, std::move(destroyBackend)}, m_DepthFormat(depthFormat),
            m_StencilFormat(stencilFormat), m_DepthStencilState(depthStencilState)
        {}
    } // namespace rhi
} // namespace vultra
