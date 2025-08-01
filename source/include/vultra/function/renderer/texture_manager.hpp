#pragma once

#include "vultra/function/renderer/texture_loader.hpp"

#include <entt/resource/cache.hpp>

namespace vultra
{
    namespace gfx
    {
        using TextureCache = entt::resource_cache<TextureResource, TextureLoader>;

        class TextureManager final : public TextureCache
        {
        public:
            explicit TextureManager(rhi::RenderDevice&);
            ~TextureManager() = default;

            [[nodiscard]] TextureResourceHandle load(const std::filesystem::path&);

        private:
            rhi::RenderDevice& m_RenderDevice;
        };
    } // namespace gfx
} // namespace vultra