#pragma once

#include <vultra/assets/texture_import.hpp>

#include <optional>

namespace vultra::asset_detail
{
    bool imageHasOpaqueAlpha(const SceneImage& image);
    bool ddsHasOpaqueAlpha(std::span<const std::byte> bytes);
    void removeOpaqueOrcaCutouts(SceneData& scene);

    SceneImage  decodeImage(std::span<const std::byte> bytes);
    SceneImage  retainDds(std::span<const std::byte> bytes);
    TextureData decodeDds(std::span<const std::byte> bytes, std::optional<bool> srgb, bool decompress);
} // namespace vultra::asset_detail
