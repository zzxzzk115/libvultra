#pragma once

#include "vultra/function/renderer/mesh_resource.hpp"

#include <glm/fwd.hpp>

namespace vultra
{
    struct AreaLightComponent;
    struct TransformComponent;

    namespace gfx
    {
        Ref<DefaultMesh> createAreaLightMesh(rhi::RenderDevice&        rd,
                                             const AreaLightComponent& lightComponent,
                                             const TransformComponent& lightTransform);
    } // namespace gfx
} // namespace vultra