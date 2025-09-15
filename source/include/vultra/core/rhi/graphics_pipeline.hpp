#pragma once

#include "vultra/core/rhi/base_pipeline.hpp"
#include "vultra/core/rhi/compare_op.hpp"
#include "vultra/core/rhi/cull_mode.hpp"
#include "vultra/core/rhi/pixel_format.hpp"
#include "vultra/core/rhi/primitive_topology.hpp"
#include "vultra/core/rhi/shader_type.hpp"
#include "vultra/core/rhi/vertex_attributes.hpp"

#include <limits>
#include <optional>
#include <span>
#include <unordered_map>

namespace vultra
{
    namespace rhi
    {
        // https://registry.khronos.org/vulkan/specs/1.3/html/chap26.html#VkStencilOp
        enum class StencilOp
        {
            // Keeps the current value.
            eKeep = VK_STENCIL_OP_KEEP,
            // Sets the value to 0.
            eZero = VK_STENCIL_OP_ZERO,
            // Sets the value to reference.
            eReplace = VK_STENCIL_OP_REPLACE,
            // Increments the current value and clamps to the maximum representable
            // unsigned value.
            eIncrementAndClamp = VK_STENCIL_OP_INCREMENT_AND_CLAMP,
            // Decrements the current value and clamps to 0.
            eDecrementAndClamp = VK_STENCIL_OP_DECREMENT_AND_CLAMP,
            // Bitwise-inverts the current value.
            eInvert = VK_STENCIL_OP_INVERT,
            // Increments the current value and wraps to 0 when the maximum value would
            // have been exceeded.
            eIncrementAndWrap = VK_STENCIL_OP_INCREMENT_AND_WRAP,
            // Decrements the current value and wraps to the maximum possible value when
            // the value would go below 0.
            eDecrementAndWrap = VK_STENCIL_OP_DECREMENT_AND_WRAP,
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

        // https://registry.khronos.org/vulkan/specs/1.3/html/chap25.html#VkPolygonMode
        enum class PolygonMode
        {
            eFill  = VK_POLYGON_MODE_FILL,
            eLine  = VK_POLYGON_MODE_LINE,
            ePoint = VK_POLYGON_MODE_POINT,
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

        // https://registry.khronos.org/vulkan/specs/1.3/html/chap27.html#VkBlendOp
        enum class BlendOp
        {
            eAdd             = VK_BLEND_OP_ADD,
            eSubtract        = VK_BLEND_OP_SUBTRACT,
            eReverseSubtract = VK_BLEND_OP_REVERSE_SUBTRACT,
            eMin             = VK_BLEND_OP_MIN,
            eMax             = VK_BLEND_OP_MAX,
        };
        // https://registry.khronos.org/vulkan/specs/1.3/html/chap27.html#VkBlendFactor
        enum class BlendFactor
        {
            eZero                  = VK_BLEND_FACTOR_ZERO,
            eOne                   = VK_BLEND_FACTOR_ONE,
            eSrcColor              = VK_BLEND_FACTOR_SRC_COLOR,
            eOneMinusSrcColor      = VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
            eDstColor              = VK_BLEND_FACTOR_DST_COLOR,
            eOneMinusDstColor      = VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR,
            eSrcAlpha              = VK_BLEND_FACTOR_SRC_ALPHA,
            eOneMinusSrcAlpha      = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            eDstAlpha              = VK_BLEND_FACTOR_DST_ALPHA,
            eOneMinusDstAlpha      = VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,
            eConstantColor         = VK_BLEND_FACTOR_CONSTANT_COLOR,
            eOneMinusConstantColor = VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR,
            eConstantAlpha         = VK_BLEND_FACTOR_CONSTANT_ALPHA,
            eOneMinusConstantAlpha = VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA,
            eSrcAlphaSaturate      = VK_BLEND_FACTOR_SRC_ALPHA_SATURATE,
            eSrc1Color             = VK_BLEND_FACTOR_SRC1_COLOR,
            eOneMinusSrc1Color     = VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR,
            eSrc1Alpha             = VK_BLEND_FACTOR_SRC1_ALPHA,
            eOneMinusSrc1Alpha     = VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA,
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

            constexpr vk::PipelineBindPoint getBindPoint() const override { return vk::PipelineBindPoint::eGraphics; }

            class Builder
            {
            public:
                Builder();
                Builder(const Builder&)     = delete;
                Builder(Builder&&) noexcept = delete;
                ~Builder()                  = default;

                Builder& operator=(const Builder&)     = delete;
                Builder& operator=(Builder&&) noexcept = delete;

                // PixelFormat can contain stencil bits (might also be Undefined).
                Builder& setDepthFormat(const PixelFormat);
                Builder& setDepthBias(const DepthBias&);
                // @param Can be empty
                Builder& setColorFormats(std::initializer_list<PixelFormat>);
                Builder& setColorFormats(std::span<const PixelFormat>);

                // Do not omit vertex attributes that your shader does not use
                // (in that case use kIgnoreVertexAttribute as an offset).
                Builder& setInputAssembly(const VertexAttributes&);
                Builder& setTopology(const PrimitiveTopology);

                Builder& setPipelineLayout(PipelineLayout);
                // If a shader of a given type is already specified, then its content will
                // be overwritten with the given code.
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
            GraphicsPipeline(const vk::Device, PipelineLayout&&, const vk::Pipeline);
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