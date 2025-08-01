#pragma once

#include "vultra/function/renderer/texture_resource_handle.hpp"

namespace vultra
{
    namespace gfx
    {
        struct TextureLoader final : entt::resource_loader<TextureResource>
        {
            result_type operator()(const std::filesystem::path&, rhi::RenderDevice&) const;
            result_type operator()(rhi::Texture&&) const;
        };
    } // namespace gfx
} // namespace vultra