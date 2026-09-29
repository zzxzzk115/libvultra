#pragma once

#include <vri/vri.h>

#include <stdexcept>

namespace vultra::asset_detail
{
    struct TextureLayout
    {
        uint32_t block;
        uint32_t bytes;
    };

    // Upload and cache validation share the formats accepted by the asset pipeline.
    inline TextureLayout textureLayout(VriFormat format)
    {
        switch (format)
        {
            case VriFormat_R8_UNORM:
                return {1, 1};
            case VriFormat_RG8_UNORM:
                return {1, 2};
            case VriFormat_RGBA8_UNORM:
            case VriFormat_RGBA8_SRGB:
            case VriFormat_BGRA8_UNORM:
            case VriFormat_BGRA8_SRGB:
                return {1, 4};
            case VriFormat_RGBA16_SFLOAT:
                return {1, 8};
            case VriFormat_RGBA32_SFLOAT:
                return {1, 16};
            case VriFormat_BC1_UNORM:
            case VriFormat_BC4_UNORM:
                return {4, 8};
            case VriFormat_BC2_UNORM:
            case VriFormat_BC3_UNORM:
            case VriFormat_BC5_UNORM:
            case VriFormat_BC6H_UFLOAT:
            case VriFormat_BC7_UNORM:
                return {4, 16};
            default:
                throw std::runtime_error("Unsupported asset texture format");
        }
    }
} // namespace vultra::asset_detail
