#pragma once

#include <vultra/assets/texture_asset.hpp>
#include <vultra/core/base/error.hpp>
#include <vultra/gpu/resources.hpp>

#include <expected>
#include <memory>

namespace vultra
{
    // Uploads every authored subresource; the caller must establish completion before replacing a live texture.
    [[nodiscard]] std::expected<std::unique_ptr<Texture>, Error> uploadTextureAsset(Device&                 device,
                                                                                    const TextureAssetData& data);
    [[nodiscard]] std::expected<std::unique_ptr<Texture>, Error> uploadTextureAsset(Device&            device,
                                                                                    const TextureData& data);
} // namespace vultra
