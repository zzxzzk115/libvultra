#pragma once

#include "vultra/core/rhi/base_pipeline.hpp"
#include "vultra/core/rhi/structs/dynamic_state.hpp"
#include "vultra/core/rhi/structs/graphics_pipeline_states.hpp"
#include "vultra/core/rhi/structs/pixel_format.hpp"
#include "vultra/core/rhi/structs/primitive_topology.hpp"
#include "vultra/core/rhi/structs/shader_type.hpp"
#include "vultra/core/rhi/structs/vertex_attributes.hpp"

#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>

namespace vultra
{
    namespace rhi
    {
        // Assign to VertexAttribute::offset in GraphicsPipeline::setInputAssembly
        // to silence "Vertex attribute at location x not consumed by vertex shader".
        constexpr auto kIgnoreVertexAttribute = std::numeric_limits<uint32_t>::max();

        class GraphicsPipeline final : public BasePipeline
        {
            friend class RenderDevice;

        public:
            GraphicsPipeline()                            = default;
            GraphicsPipeline(const GraphicsPipeline&)     = delete;
            GraphicsPipeline(GraphicsPipeline&&) noexcept = default;

            GraphicsPipeline& operator=(const GraphicsPipeline&)     = delete;
            GraphicsPipeline& operator=(GraphicsPipeline&&) noexcept = default;

            constexpr PipelineBindPoint getBindPoint() const override { return PipelineBindPoint::eGraphics; }

            class Builder
            {
            public:
                Builder();
                Builder(const Builder&)     = delete;
                Builder(Builder&&) noexcept = delete;
                ~Builder();

                Builder& operator=(const Builder&)     = delete;
                Builder& operator=(Builder&&) noexcept = delete;

                Builder& setDepthFormat(const PixelFormat);
                Builder& setDepthBias(const DepthBias&);

                Builder& setColorFormats(std::initializer_list<PixelFormat>);
                Builder& setColorFormats(std::span<const PixelFormat>);
                Builder& setViewMask(uint32_t);

                Builder& setInputAssembly(const VertexAttributes&);
                Builder& setVertexStride(uint32_t);
                Builder& setTopology(const PrimitiveTopology);

                Builder& setPipelineLayout(PipelineLayout);

                Builder& addShader(const ShaderType, const ShaderStageInfo&);
                Builder& addBuiltinShader(const ShaderType, const SPIRV&);

                Builder& setDepthStencil(const DepthStencilState&);
                Builder& setRasterizer(const RasterizerState&);
                Builder& setBlending(const AttachmentIndex, const BlendState&);
                Builder& setDynamicState(std::initializer_list<DynamicState>);

                [[nodiscard]] GraphicsPipeline build(RenderDevice&);

            private:
                [[nodiscard]] std::optional<GraphicsPipeline> buildWebGPU(RenderDevice&);
                [[nodiscard]] GraphicsPipeline                buildVulkan(RenderDevice&);

                PixelFormat             m_DepthFormat {PixelFormat::eUndefined};
                PixelFormat             m_StencilFormat {PixelFormat::eUndefined};
                std::vector<PixelFormat> m_ColorAttachmentFormats;
                uint32_t                m_ViewMask {0};

                VertexAttributes m_VertexAttributes;
                uint32_t         m_VertexStride {0};
                PrimitiveTopology m_PrimitiveTopology {PrimitiveTopology::eTriangleList};

                std::unordered_map<ShaderType, ShaderStageInfo> m_ShaderStages;
                std::unordered_map<ShaderType, SPIRV>           m_BuiltinShaderStages;
                PipelineLayout                                  m_PipelineLayout;

                DepthStencilState               m_DepthStencilState {};
                RasterizerState                 m_RasterizerState {};
                std::vector<BlendState>         m_BlendStates;
                std::vector<DynamicState> m_DynamicStates {DynamicState::eViewport, DynamicState::eScissor};
            };

        private:
            GraphicsPipeline(PipelineLayout&&, std::uintptr_t, std::unique_ptr<IPipeline>);
        };
    } // namespace rhi
} // namespace vultra
