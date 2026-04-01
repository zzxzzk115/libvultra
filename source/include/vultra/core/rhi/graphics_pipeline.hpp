#pragma once

#include "vultra/core/rhi/base_pipeline.hpp"
#include "vultra/core/rhi/structs/compare_op.hpp"
#include "vultra/core/rhi/structs/cull_mode.hpp"
#include "vultra/core/rhi/structs/pixel_format.hpp"
#include "vultra/core/rhi/structs/primitive_topology.hpp"
#include "vultra/core/rhi/structs/shader_type.hpp"
#include "vultra/core/rhi/structs/vertex_attributes.hpp"

#include <limits>
#include <optional>
#include <span>
#include <unordered_map>

namespace vultra
{
    namespace rhi
    {
        enum class StencilOp
        {
            eKeep,
            eZero,
            eReplace,
            eIncrementAndClamp,
            eDecrementAndClamp,
            eInvert,
            eIncrementAndWrap,
            eDecrementAndWrap,
        };

        struct StencilOpState
        {
            // Specifies the action performed on samples that fail the stencil test.
            StencilOp failOp {StencilOp::eKeep};
            // Specifies the action performed on samples that pass both the depth and
            // stencil tests.
            StencilOp passOp {StencilOp::eKeep};
            // Specifies the action performed on samples that pass the stencil test and
            // fail the depth test.
            StencilOp depthFailOp {StencilOp::eKeep};
            // Specifies the comparison operator used in the stencil test.
            CompareOp compareOp {CompareOp::eAlways};
            // Selects the bits of the unsigned integer stencil values participating in
            // the stencil test.
            uint8_t compareMask {0xFF};
            // Selects the bits of the unsigned integer stencil values updated by the
            // stencil test in the stencil framebuffer attachment.
            uint8_t writeMask {0xFF};
            // Stencil reference value that is used in the unsigned stencil comparison.
            uint32_t reference {0};
        };

        struct DepthStencilState
        {
            // Controls whether depth testing is enabled.
            bool depthTest {false};
            // Controls whether depth writes are enabled when depthTest is true.
            // Depth writes are always disabled when depthTest is false.
            bool depthWrite {true};
            // Specifies the function used to compare each incoming pixel depth value with
            // the depth value present in the depth buffer. The comparison is performed
            // only if depth testing is enabled.
            CompareOp depthCompareOp {CompareOp::eLessOrEqual};
            // Controls whether stencil testing is enabled.
            bool                          stencilTestEnable {false};
            StencilOpState                front {};
            std::optional<StencilOpState> back {std::nullopt};
        };

        enum class PolygonMode
        {
            eFill,
            eLine,
            ePoint,
        };

        struct DepthBias
        {
            // Scalar factor controlling the constant depth value added to each fragment.
            float constantFactor {0.0f};
            // The maximum (or minimum) depth bias of a fragment.
            // Scalar factor applied to a fragment�s slope in depth bias calculations.
            float slopeFactor {0.0f};
        };

        struct RasterizerState
        {
            // The triangle rendering mode.
            PolygonMode polygonMode {PolygonMode::eFill};
            // Specify whether front- or back-facing facets can be culled.
            CullMode                 cullMode {CullMode::eNone};
            std::optional<DepthBias> depthBias;
            // Controls whether to clamp the fragment�s depth values as described in Depth
            // Test. Enabling depth clamp will also disable clipping primitives to the z
            // planes of the frustrum as described in Primitive Clipping.
            bool depthClampEnable {false};
            // The width of rasterized line segments.
            float lineWidth {1.0f};
        };

        enum class BlendOp
        {
            eAdd,
            eSubtract,
            eReverseSubtract,
            eMin,
            eMax,
        };
        enum class BlendFactor
        {
            eZero,
            eOne,
            eSrcColor,
            eOneMinusSrcColor,
            eDstColor,
            eOneMinusDstColor,
            eSrcAlpha,
            eOneMinusSrcAlpha,
            eDstAlpha,
            eOneMinusDstAlpha,
            eConstantColor,
            eOneMinusConstantColor,
            eConstantAlpha,
            eOneMinusConstantAlpha,
            eSrcAlphaSaturate,
            eSrc1Color,
            eOneMinusSrc1Color,
            eSrc1Alpha,
            eOneMinusSrc1Alpha,
        };

        // src = Incoming values (fragment shader output).
        // dst = Values already present in a framebuffer.
        struct BlendState
        {
            // Controls whether blending is enabled for the corresponding color
            // attachment. If blending is not enabled, the source fragment�s color for
            // that attachment is passed through unmodified.
            bool enabled {false};

            // Selects which blend factor is used to determine the source factors
            BlendFactor srcColor {BlendFactor::eOne};
            // Selects which blend factor is used to determine the destination factors
            BlendFactor dstColor {BlendFactor::eZero};
            // Selects which blend operation is used to calculate the RGB values to write
            // to the color attachment.
            BlendOp colorOp {BlendOp::eAdd};

            // Selects which blend factor is used to determine the source factor.
            BlendFactor srcAlpha {BlendFactor::eOne};
            // Selects which blend factor is used to determine the destination factor.
            BlendFactor dstAlpha {BlendFactor::eZero};
            // Selects which blend operation is used to calculate the alpha values to
            // write to the color attachment.
            BlendOp alphaOp {BlendOp::eAdd};
        };

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
                ~Builder()                  = default;

                Builder& operator=(const Builder&)     = delete;
                Builder& operator=(Builder&&) noexcept = delete;

                Builder& setDepthFormat(const PixelFormat);
                Builder& setDepthBias(const DepthBias&);

                Builder& setColorFormats(std::initializer_list<PixelFormat>);
                Builder& setColorFormats(std::span<const PixelFormat>);
                Builder& setViewMask(uint32_t);

                Builder& setInputAssembly(const VertexAttributes&);
                Builder& setTopology(const PrimitiveTopology);

                Builder& setPipelineLayout(PipelineLayout);

                Builder& addShader(const ShaderType, const ShaderStageInfo&);
                Builder& addBuiltinShader(const ShaderType, const SPIRV&);

                Builder& setDepthStencil(const DepthStencilState&);
                Builder& setRasterizer(const RasterizerState&);
                Builder& setBlending(const AttachmentIndex, const BlendState&);
                Builder& setDynamicState(std::initializer_list<vk::DynamicState>);

                [[nodiscard]] GraphicsPipeline build(RenderDevice&);

            private:
                vk::Format              m_DepthFormat {vk::Format::eUndefined};
                vk::Format              m_StencilFormat {vk::Format::eUndefined};
                std::vector<vk::Format> m_ColorAttachmentFormats;
                uint32_t                m_ViewMask {0};

                vk::VertexInputBindingDescription                m_VertexInput;
                std::vector<vk::VertexInputAttributeDescription> m_VertexInputAttributes;
                vk::PrimitiveTopology m_PrimitiveTopology {vk::PrimitiveTopology::eTriangleList};

                std::unordered_map<ShaderType, ShaderStageInfo> m_ShaderStages;
                std::unordered_map<ShaderType, SPIRV>           m_BuiltinShaderStages;
                PipelineLayout                                  m_PipelineLayout;

                vk::PipelineDepthStencilStateCreateInfo            m_DepthStencilState;
                vk::PipelineRasterizationStateCreateInfo           m_RasterizerState;
                std::vector<vk::PipelineColorBlendAttachmentState> m_BlendStates;
                std::vector<vk::DynamicState> m_DynamicStates {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
            };

        private:
            GraphicsPipeline(std::uintptr_t, PipelineLayout&&, std::uintptr_t);
        };
    } // namespace rhi
} // namespace vultra

namespace std
{
    template<>
    struct hash<vultra::rhi::BlendState>
    {
        size_t operator()(const vultra::rhi::BlendState& b) const noexcept
        {
            return (std::hash<bool> {}(b.enabled) ^
                    (std::hash<std::underlying_type_t<vultra::rhi::BlendFactor>> {}(
                         static_cast<std::underlying_type_t<vultra::rhi::BlendFactor>>(b.srcColor))
                     << 1) ^
                    (std::hash<std::underlying_type_t<vultra::rhi::BlendFactor>> {}(
                         static_cast<std::underlying_type_t<vultra::rhi::BlendFactor>>(b.dstColor))
                     << 2) ^
                    (std::hash<std::underlying_type_t<vultra::rhi::BlendOp>> {}(
                         static_cast<std::underlying_type_t<vultra::rhi::BlendOp>>(b.colorOp))
                     << 3) ^
                    (std::hash<std::underlying_type_t<vultra::rhi::BlendFactor>> {}(
                         static_cast<std::underlying_type_t<vultra::rhi::BlendFactor>>(b.srcAlpha))
                     << 4) ^
                    (std::hash<std::underlying_type_t<vultra::rhi::BlendFactor>> {}(
                         static_cast<std::underlying_type_t<vultra::rhi::BlendFactor>>(b.dstAlpha))
                     << 5) ^
                    (std::hash<std::underlying_type_t<vultra::rhi::BlendOp>> {}(
                         static_cast<std::underlying_type_t<vultra::rhi::BlendOp>>(b.alphaOp))
                     << 6));
        }
    };
} // namespace std

