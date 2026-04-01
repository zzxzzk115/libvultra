#pragma once

#include "vultra/core/rhi/structs/compare_op.hpp"
#include "vultra/core/rhi/structs/texel_filter.hpp"

#include <cstdint>
#include <optional>

namespace vultra
{
    namespace rhi
    {
        enum class MipmapMode
        {
            eNearest,
            eLinear,
        };

        enum class SamplerAddressMode
        {
            eRepeat,
            eMirroredRepeat,
            eClampToEdge,
            eClampToBorder,
            eMirrorClampToEdge,
        };

        enum class BorderColor
        {
            eFloatTransparentBlack,
            eFloatOpaqueBlack,
            eFloatOpaqueWhite,
        };

        struct SamplerInfo
        {
            TexelFilter               magFilter {TexelFilter::eLinear};
            TexelFilter               minFilter {TexelFilter::eLinear};
            MipmapMode                mipmapMode {MipmapMode::eLinear};
            SamplerAddressMode        addressModeS {SamplerAddressMode::eRepeat};
            SamplerAddressMode        addressModeT {SamplerAddressMode::eRepeat};
            SamplerAddressMode        addressModeR {SamplerAddressMode::eRepeat};
            std::optional<float>      maxAnisotropy;
            std::optional<CompareOp>  compareOp;
            float                     minLod {0.0f};
            float                     maxLod {0.0f};
            BorderColor               borderColor {BorderColor::eFloatOpaqueBlack};
        };
    } // namespace rhi
} // namespace vultra
