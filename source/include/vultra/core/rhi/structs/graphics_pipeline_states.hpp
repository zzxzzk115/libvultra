#pragma once

#include "vultra/core/rhi/structs/compare_op.hpp"
#include "vultra/core/rhi/structs/cull_mode.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <type_traits>

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
            StencilOp failOp {StencilOp::eKeep};
            StencilOp passOp {StencilOp::eKeep};
            StencilOp depthFailOp {StencilOp::eKeep};
            CompareOp compareOp {CompareOp::eAlways};
            uint8_t   compareMask {0xFF};
            uint8_t   writeMask {0xFF};
            uint32_t  reference {0};
        };

        struct DepthStencilState
        {
            bool                          depthTest {false};
            bool                          depthWrite {true};
            CompareOp                     depthCompareOp {CompareOp::eLessOrEqual};
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
            float constantFactor {0.0f};
            float slopeFactor {0.0f};
        };

        struct RasterizerState
        {
            PolygonMode              polygonMode {PolygonMode::eFill};
            CullMode                 cullMode {CullMode::eNone};
            std::optional<DepthBias> depthBias;
            bool                     depthClampEnable {false};
            float                    lineWidth {1.0f};
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

        struct BlendState
        {
            bool        enabled {false};
            BlendFactor srcColor {BlendFactor::eOne};
            BlendFactor dstColor {BlendFactor::eZero};
            BlendOp     colorOp {BlendOp::eAdd};
            BlendFactor srcAlpha {BlendFactor::eOne};
            BlendFactor dstAlpha {BlendFactor::eZero};
            BlendOp     alphaOp {BlendOp::eAdd};
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
