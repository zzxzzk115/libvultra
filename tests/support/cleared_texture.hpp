#pragma once

#include <vultra/gpu/resources.hpp>

#include <algorithm>
#include <array>

namespace support
{
    // A color target that a test always clears to `clear`. The texture is created with that clear value, because
    // D3D12 warns about a clear to any other value.
    inline VriTextureDesc
    clearedTexture(vultra::Extent extent, const std::array<float, 4>& clear, VriFormat format = VriFormat_RGBA8_UNORM)
    {
        auto desc = vultra::colorTexture(extent, format);
        std::ranges::copy(clear, desc.clearValue.color.f32);
        return desc;
    }
} // namespace support
