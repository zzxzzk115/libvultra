#pragma once
#include <vultra/core/image/image.hpp>
#include <vultra/core/rhi/resources.hpp>

namespace vultra
{
    // Blocking readback between frames. Source needs TransferSrc and initialized contents.
    // RGBA/BGRA8 and RGBA16/32F only; floats stay floats until SavePng explicitly quantizes them.
    Image readback(Device& device, Texture& texture, uint32_t mip = 0);
} // namespace vultra
