#pragma once

#include "vultra/function/renderer/mesh_resource.hpp"

#include <glm/fwd.hpp>

namespace vultra
{
    namespace gfx
    {
        Ref<DefaultMesh> createAreaLightMesh(rhi::RenderDevice& rd,
                                             const glm::vec3&   position,
                                             float              width,
                                             float              height,
                                             const glm::vec4&   emissiveColorIntensity,
                                             bool               twoSided);
    } // namespace gfx
} // namespace vultra