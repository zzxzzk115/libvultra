#pragma once

#include <vultra/assets/texture_format.hpp>

#include <stdexcept>

namespace vultra::asset_detail
{
    struct TextureLayout
    {
        uint32_t block;
        uint32_t bytes;
    };

    // Upload and cache validation share the formats accepted by the asset pipeline.
    inline TextureLayout textureLayout(TextureFormat format)
    {
        switch (format)
        {
            case TextureFormat::eR8Unorm:
                return {1, 1};
            case TextureFormat::eRg8Unorm:
                return {1, 2};
            case TextureFormat::eRgba8Unorm:
            case TextureFormat::eRgba8Srgb:
            case TextureFormat::eBgra8Unorm:
            case TextureFormat::eBgra8Srgb:
                return {1, 4};
            case TextureFormat::eRgba16Sfloat:
                return {1, 8};
            case TextureFormat::eRgba32Sfloat:
                return {1, 16};
            case TextureFormat::eBc1Unorm:
            case TextureFormat::eBc4Unorm:
                return {4, 8};
            case TextureFormat::eBc2Unorm:
            case TextureFormat::eBc3Unorm:
            case TextureFormat::eBc5Unorm:
            case TextureFormat::eBc6hUfloat:
            case TextureFormat::eBc7Unorm:
                return {4, 16};
            default:
                throw std::runtime_error("Unsupported asset texture format");
        }
    }
} // namespace vultra::asset_detail
