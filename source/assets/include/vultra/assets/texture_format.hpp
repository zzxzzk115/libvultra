#pragma once

#include <cstdint>

namespace vultra
{
    // CPU asset formats are independent of the graphics device's format enum.
    enum class TextureFormat : uint32_t
    {
        eUnknown,
        eR8Unorm,
        eRg8Unorm,
        eRgba8Unorm,
        eRgba8Srgb,
        eBgra8Unorm,
        eBgra8Srgb,
        eRgba16Sfloat,
        eRgba32Sfloat,
        eBc1Unorm,
        eBc2Unorm,
        eBc3Unorm,
        eBc4Unorm,
        eBc5Unorm,
        eBc6hUfloat,
        eBc7Unorm,
        // Appended to retain existing prepared-scene and cache enum values.
        eBc1Srgb,
        eBc2Srgb,
        eBc3Srgb,
        eBc7Srgb,
        eAstc4x4Unorm,
        eAstc4x4Srgb,
        eAstc6x6Unorm,
        eAstc6x6Srgb,
        // ASTC payload stores source RG in the red and green channels; consumers may reconstruct normal Z.
        eAstc4x4RgUnorm,
        eAstc6x6RgUnorm
    };
} // namespace vultra
