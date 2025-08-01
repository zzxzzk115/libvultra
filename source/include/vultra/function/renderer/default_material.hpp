#pragma once

#include "vultra/core/rhi/texture.hpp"

namespace vultra
{
    namespace gfx
    {
        struct PBRMaterial
        {
            Ref<rhi::Texture> albedo {nullptr};
            Ref<rhi::Texture> metallicRoughness {nullptr};
            Ref<rhi::Texture> normal {nullptr};
            Ref<rhi::Texture> ao {nullptr};
            Ref<rhi::Texture> emissive {nullptr};
        };
    } // namespace gfx
} // namespace vultra