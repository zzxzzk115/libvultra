#pragma once

#include "vultra/core/base/base.hpp"
#include "vultra/core/rhi/texture.hpp"

#include <cstdint>

namespace vultra::resource
{
    struct GpuTexture
    {
        Ref<rhi::Texture> texture {nullptr};
        uint32_t          bindlessIndex {0}; // 0 is reserved for fallback texture
    };
}
