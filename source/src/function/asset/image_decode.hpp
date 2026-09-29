#pragma once

#include <vultra/function/asset/texture_import.hpp>

#include <optional>

namespace vultra::asset_detail
{
    SceneImage  decodeImage(std::span<const std::byte> bytes);
    SceneImage  retainDds(std::span<const std::byte> bytes);
    TextureData decodeDds(std::span<const std::byte> bytes, std::optional<bool> srgb, bool decompress);
} // namespace vultra::asset_detail
