#pragma once

#include <vultra/assets/texture_import.hpp>

#include <optional>

namespace vultra::asset_detail
{
    std::expected<bool, std::string> imageHasOpaqueAlphaExpected(const SceneImage& image);
    std::expected<bool, std::string> ddsHasOpaqueAlphaExpected(std::span<const std::byte> bytes);
    std::expected<void, std::string> removeOpaqueOrcaCutoutsExpected(SceneData& scene);

    std::expected<SceneImage, std::string> decodeImageExpected(std::span<const std::byte> bytes);
    std::expected<SceneImage, std::string> retainDdsExpected(std::span<const std::byte> bytes);
    std::expected<TextureData, std::string>
               decodeDdsExpected(std::span<const std::byte> bytes, std::optional<bool> srgb, bool decompress);
    SceneImage decodeImage(std::span<const std::byte> bytes);
} // namespace vultra::asset_detail
