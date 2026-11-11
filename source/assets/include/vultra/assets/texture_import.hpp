#pragma once

#include <vultra/assets/scene_render_data.hpp>
#include <vultra/core/base/error.hpp>

#include <expected>

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
    [[nodiscard]] std::expected<TextureData, Error> loadDds(const std::filesystem::path& path,
                                                            const SourceObserver&        observer = {},
                                                            const AssetSource*           source   = nullptr);

    [[nodiscard]] std::expected<TextureData, Error>
    prepareTexture(const SceneImage& image, bool srgb, const TextureImportOptions& options = {});
    [[nodiscard]] std::expected<PreparedTextures, Error>
    prepareTextures(const SceneData& scene, const TextureImportOptions& options = {}, uint32_t workers = 0);
} // namespace vultra
