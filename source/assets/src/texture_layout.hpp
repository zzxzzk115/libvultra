#pragma once

#include <vultra/assets/texture_format.hpp>
#include <vultra/core/base/error.hpp>

#include <expected>

namespace vultra::asset_detail
{
    struct TextureLayout
    {
        uint32_t block;
        uint32_t bytes;
    };

    // Upload and cache validation share the formats accepted by the asset pipeline.
    [[nodiscard]] inline std::expected<TextureLayout, Error> textureLayout(TextureFormat format)
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
            case TextureFormat::eBc4Unorm:
                return TextureLayout {4, 8};
            case TextureFormat::eBc2Unorm:
            case TextureFormat::eBc3Unorm:
            case TextureFormat::eBc5Unorm:
            case TextureFormat::eBc6hUfloat:
            case TextureFormat::eBc7Unorm:
                return TextureLayout {4, 16};
            default:
                return makeError("Unsupported asset texture format");
        }
    }
} // namespace vultra::asset_detail
