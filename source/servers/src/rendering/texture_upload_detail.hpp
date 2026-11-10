#pragma once

#include <vultra/servers/rendering/texture_upload.hpp>

namespace vultra::detail
{
    struct TextureUploadError
    {
        std::string message;
        bool        invalidInput;
    };

    std::expected<std::unique_ptr<Texture>, TextureUploadError> uploadTextureAsset(Device&            device,
                                                                                   const TextureData& data);
} // namespace vultra::detail
