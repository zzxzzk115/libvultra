#pragma once

#include "vultra/core/rhi/compare_op.hpp"
#include "vultra/core/rhi/texel_filter.hpp"

#include <optional>

namespace vultra
{
    namespace rhi
    {
        // https://registry.khronos.org/vulkan/specs/1.3/html/chap13.html#VkSamplerMipmapMode
        enum class MipmapMode
        {
            eNearest = VK_SAMPLER_MIPMAP_MODE_NEAREST,
            eLinear  = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        };

        // https://registry.khronos.org/vulkan/specs/1.3/html/chap13.html#VkSamplerAddressMode
        enum class SamplerAddressMode
        {
            eRepeat            = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            eMirroredRepeat    = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
            eClampToEdge       = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            eClampToBorder     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
            eMirrorClampToEdge = VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE,
        };

        // https://registry.khronos.org/vulkan/specs/1.3/html/chap13.html#VkBorderColor
        enum class BorderColor
        {
            eTransparentBlack = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
            eOpaqueBlack      = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
            eOpaqueWhite      = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
        };

        struct SamplerInfo
        {
            TexelFilter magFilter {TexelFilter::eLinear};
            TexelFilter minFilter {TexelFilter::eNearest};
            MipmapMode  mipmapMode {MipmapMode::eLinear};

            SamplerAddressMode addressModeS {SamplerAddressMode::eRepeat};
            SamplerAddressMode addressModeT {SamplerAddressMode::eRepeat};
            SamplerAddressMode addressModeR {SamplerAddressMode::eRepeat};

            // The anisotropy value clamp used by the sampler.
            std::optional<float>     maxAnisotropy {std::nullopt};
            std::optional<CompareOp> compareOp {std::nullopt};

            // https://registry.khronos.org/vulkan/specs/1.3/html/chap16.html#textures-level-of-detail-operation

            float minLod {0.0f};
            float maxLod {1.0f};

            BorderColor borderColor {BorderColor::eOpaqueBlack};
        };
    } // namespace rhi
} // namespace vultra