#pragma once

#include <vultra/assets/texture_format.hpp>

#include <expected>
#include <string>

namespace vultra::asset_detail
{
    struct TextureLayout
    {
        uint32_t block;
        uint32_t bytes;
    };

    // Upload and cache validation share the formats accepted by the asset pipeline.
    inline std::expected<TextureLayout, std::string> textureLayout(TextureFormat format)
    {
        switch (format)
        {
            case TextureFormat::eR8Unorm:
                return TextureLayout {1, 1};
            case TextureFormat::eRg8Unorm:
                return TextureLayout {1, 2};
            case TextureFormat::eRgba8Unorm:
            case TextureFormat::eRgba8Srgb:
            case TextureFormat::eBgra8Unorm:
            case TextureFormat::eBgra8Srgb:
                return TextureLayout {1, 4};
            case TextureFormat::eRgba16Sfloat:
                return TextureLayout {1, 8};
            case TextureFormat::eRgba32Sfloat:
                return TextureLayout {1, 16};
            case TextureFormat::eBc1Unorm:
            case TextureFormat::eBc1Srgb:
            case TextureFormat::eBc4Unorm:
                return TextureLayout {4, 8};
            case TextureFormat::eBc2Unorm:
            case TextureFormat::eBc2Srgb:
            case TextureFormat::eBc3Unorm:
            case TextureFormat::eBc3Srgb:
            case TextureFormat::eBc5Unorm:
            case TextureFormat::eBc6hUfloat:
            case TextureFormat::eBc7Unorm:
            case TextureFormat::eBc7Srgb:
            case TextureFormat::eAstc4x4Unorm:
            case TextureFormat::eAstc4x4Srgb:
            case TextureFormat::eAstc4x4RgUnorm:
                return TextureLayout {4, 16};
            case TextureFormat::eAstc6x6Unorm:
            case TextureFormat::eAstc6x6Srgb:
            case TextureFormat::eAstc6x6RgUnorm:
                return TextureLayout {6, 16};
            default:
                return std::unexpected("Unsupported asset texture format");
        }
    }
} // namespace vultra::asset_detail
