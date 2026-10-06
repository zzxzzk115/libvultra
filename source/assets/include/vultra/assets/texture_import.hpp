#pragma once

#include <vultra/assets/scene_data.hpp>
#include <vultra/assets/texture_format.hpp>

namespace vultra
{
    struct TextureLevel
    {
        Extent                 size;
        std::vector<std::byte> bytes;
    };

    struct TextureData
    {
        TextureFormat             format = TextureFormat::eUnknown;
        std::vector<TextureLevel> levels;
        // Import provenance: leading authored mips are restored from the source, never cached.
        int      sourceImage  = -1;
        uint32_t sourceMipNum = 0;
    };

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

    struct PreparedTextures
    {
        std::vector<TextureData>                                 images;
        std::vector<std::array<uint32_t, kMaterialTextureCount>> materials;
    };

    // Static 2D DDS with its authored mips and color-space metadata. Arrays/cubes/volumes are rejected.
    TextureData loadDds(const std::filesystem::path& path,
                        const SourceObserver&        observer = {},
                        const AssetSource*           source   = nullptr);

    TextureData prepareTexture(const SceneImage& image, bool srgb, const TextureImportOptions& options = {});
    PreparedTextures
    prepareTextures(const SceneData& scene, const TextureImportOptions& options = {}, uint32_t workers = 0);
} // namespace vultra
