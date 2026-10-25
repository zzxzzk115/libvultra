#pragma once

#include <vultra/assets/scene_data.hpp>
#include <vultra/assets/texture_data.hpp>

namespace vultra
{
    struct PreparedTextures
    {
        std::vector<TextureData>                                 images;
        std::vector<std::array<uint32_t, kMaterialTextureCount>> materials;
    };

} // namespace vultra
