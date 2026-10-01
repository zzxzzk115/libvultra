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
        eBc7Unorm
    };
} // namespace vultra
