#pragma once

#include <entt/locator/locator.hpp>

namespace vultra
{
    namespace rhi
    {
        class RenderDevice;
    }

    namespace gfx
    {
        class MeshManager;
        class TextureManager;
    } // namespace gfx

    namespace service
    {
        struct Services
        {
            Services() = delete;

            static void init(rhi::RenderDevice&);
            static void reset();

            struct Resources
            {
                Resources() = delete;

                static void clear();

                using Meshes   = entt::locator<gfx::MeshManager>;
                using Textures = entt::locator<gfx::TextureManager>;
            };
        };
    } // namespace service
} // namespace vultra