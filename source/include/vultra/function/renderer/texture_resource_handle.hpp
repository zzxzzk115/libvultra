#pragma once

#include "vultra/function/renderer/texture_resource.hpp"

#include <entt/resource/resource.hpp>

namespace vultra
{
    namespace gfx
    {
        using TextureResourceHandle = entt::resource<TextureResource>;
    }
} // namespace vultra