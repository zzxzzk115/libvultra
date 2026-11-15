#pragma once

#include <vultra/assets/scene_render_data.hpp>
#include <vultra/core/base/error.hpp>

#include <expected>

namespace vultra
{
    enum class TextureCompression
    {
        eNone,
        // Linear textures whose size is a multiple of 4 become UASTC, which the renderer transcodes to the first
        // of ASTC 4x4, BC7, ETC2 and RGBA8 that the device samples. Color textures stay RGBA8 for hardware sRGB
        // filtering: those formats are UNORM here.
        eUastcLinear
    };

    struct TextureImportOptions
    {
        bool               mipmaps     = true;
        TextureCompression compression = TextureCompression::eUastcLinear;
    };

    // Static 2D DDS with its authored mips and color-space metadata. Arrays/cubes/volumes are rejected.
    [[nodiscard]] std::expected<TextureData, Error> loadDds(const std::filesystem::path& path,
                                                            const SourceObserver&        observer = {},
                                                            const AssetSource*           source   = nullptr);

    [[nodiscard]] std::expected<TextureData, Error>
    prepareTexture(const SceneImage& image, bool srgb, const TextureImportOptions& options = {});
    [[nodiscard]] std::expected<PreparedTextures, Error>
    prepareTextures(const SceneData& scene, const TextureImportOptions& options = {}, uint32_t workers = 0);
} // namespace vultra
