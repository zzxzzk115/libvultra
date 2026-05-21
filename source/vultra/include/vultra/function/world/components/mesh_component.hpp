#pragma once

#include "vultra/core/base/uuid.hpp"

#include <glm/vec4.hpp>

#include <cstdint>

namespace vultra
{
    struct MeshComponent
    {
        CoreUUID mesh;
        // UINT32_MAX = external/imported mesh UUID in `mesh`.
        // 0 = quad, 1 = cube, 2 = sphere, 3 = capsule.
        uint32_t builtinGeometry {UINT32_MAX};
        glm::vec4 materialColor {1.0f};
    };
} // namespace vultra
