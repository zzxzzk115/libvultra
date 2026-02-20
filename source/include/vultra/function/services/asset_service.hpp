#pragma once

#include "vultra/core/base/base.hpp"

#include <vbase/core/uuid.hpp>
#include <vbase/service/service_registry.hpp>

#include <string_view>

namespace vultra::gfx
{
    class Mesh;
}

namespace vultra::rhi
{
    class Texture;
}

namespace vultra
{
    class IAssetService
    {
    public:
        SERVICE_REGISTER(IAssetService)

        virtual Ref<gfx::Mesh> loadMesh(std::string_view sourceUri) = 0;
        virtual Ref<gfx::Mesh> loadMesh(vbase::UUID uuid)           = 0;

        virtual Ref<rhi::Texture> loadTexture(std::string_view sourceUri) = 0;
        virtual Ref<rhi::Texture> loadTexture(vbase::UUID uuid)           = 0;
    };
} // namespace vultra