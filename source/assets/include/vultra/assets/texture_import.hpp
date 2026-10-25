#pragma once

#include <vultra/assets/scene_render_data.hpp>

namespace vultra
{
    enum class TextureCompression
    {
        eNone,
        // VRI currently exposes BC7_UNORM. Color textures retain hardware sRGB filtering.
        eBc7Linear
    };

    struct TextureImportOptions
    {
        bool               mipmaps     = true;
        TextureCompression compression = TextureCompression::eBc7Linear;
    };

    // Static 2D DDS with its authored mips and color-space metadata. Arrays/cubes/volumes are rejected.
    TextureData loadDds(const std::filesystem::path& path,
                        const SourceObserver&        observer = {},
                        const AssetSource*           source   = nullptr);

    TextureData prepareTexture(const SceneImage& image, bool srgb, const TextureImportOptions& options = {});
    PreparedTextures
    prepareTextures(const SceneData& scene, const TextureImportOptions& options = {}, uint32_t workers = 0);
} // namespace vultra
