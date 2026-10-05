#pragma once

#include <vultra/assets/texture_asset.hpp>
#include <vultra/drivers/rhi/resources.hpp>

#include <memory>

namespace vultra
{
    // Uploads every authored subresource; the caller must establish completion before replacing a live texture.
    std::unique_ptr<Texture> uploadTextureAsset(Device& device, const TextureAssetData& data);
} // namespace vultra
