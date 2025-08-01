#pragma once

#include "vultra/core/rhi/texture.hpp"
#include "vultra/function/resource/resource.hpp"

#include <filesystem>

namespace vultra
{
    namespace gfx
    {
        class TextureResource final : public resource::Resource, public rhi::Texture
        {
        public:
            TextureResource() = default;
            TextureResource(rhi::Texture&&, const std::filesystem::path&);
            TextureResource(const TextureResource&)     = delete;
            TextureResource(TextureResource&&) noexcept = default;

            TextureResource& operator=(const TextureResource&)     = delete;
            TextureResource& operator=(TextureResource&&) noexcept = default;
        };
    } // namespace gfx
} // namespace vultra