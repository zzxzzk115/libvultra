#pragma once
#include <vultra/core/image/image.hpp>
#include <vultra/drivers/rhi/resources.hpp>

namespace vultra
{
    // Blocking readback between frames. Source needs TransferSrc and initialized contents.
    // RGBA/BGRA8, RGBA16/32F and D32F; depth is replicated to RGB. Floats stay floats until savePng quantizes them.
    Image readback(Device& device, Texture& texture, uint32_t mip = 0);
} // namespace vultra
