#pragma once

#include <vultra/assets/texture_import.hpp>
#include <vultra/core/base/error.hpp>

#include <expected>
#include <optional>

namespace vultra::asset_detail
{
    [[nodiscard]] std::expected<bool, Error> imageHasOpaqueAlpha(const SceneImage& image);
    [[nodiscard]] std::expected<bool, Error> ddsHasOpaqueAlpha(std::span<const std::byte> bytes);
    [[nodiscard]] std::expected<void, Error> removeOpaqueOrcaCutouts(SceneData& scene);

    [[nodiscard]] std::expected<SceneImage, Error> decodeImage(std::span<const std::byte> bytes);
    [[nodiscard]] std::expected<SceneImage, Error> retainDds(std::span<const std::byte> bytes);
    [[nodiscard]] std::expected<TextureData, Error>
    decodeDds(std::span<const std::byte> bytes, std::optional<bool> srgb, bool decompress);
} // namespace vultra::asset_detail
