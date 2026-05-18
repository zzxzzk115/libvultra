#pragma once

#include "vultra/function/renderer/texture_resource_handle.hpp"

#include <cstdint>

namespace vultra
{
    namespace gfx
    {
        enum class TextureColorSpace : uint8_t
        {
            eAuto = 0,
            eLinear,
            eSRGB,
        };

        struct TextureLoader final : entt::resource_loader<TextureResource>
        {
            result_type
            operator()(const std::filesystem::path&, rhi::RenderDevice&, TextureColorSpace = TextureColorSpace::eAuto)
                const;
            result_type operator()(rhi::Texture&&) const;
        };
    } // namespace gfx
} // namespace vultra
