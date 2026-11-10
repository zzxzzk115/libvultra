#pragma once

#include <vultra/assets/texture_asset.hpp>
#include <vultra/drivers/rhi/resources.hpp>

#include <expected>
#include <memory>
#include <string>

namespace vultra
{
    // Uploads every authored subresource; the caller must establish completion before replacing a live texture.
    std::unique_ptr<Texture> uploadTextureAsset(Device& device, const TextureAssetData& data);
    std::unique_ptr<Texture> uploadTextureAsset(Device& device, const TextureData& data);
    std::expected<std::unique_ptr<Texture>, std::string> uploadTextureAssetExpected(Device&                 device,
                                                                                    const TextureAssetData& data);
    std::expected<std::unique_ptr<Texture>, std::string> uploadTextureAssetExpected(Device&            device,
                                                                                    const TextureData& data);
} // namespace vultra
