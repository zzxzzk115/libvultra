#pragma once

#include "vultra/core/base/uuid.hpp"

#include <glm/vec4.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace vultra
{
    struct MaterialSlotOverride
    {
        uint32_t    slot {0};
        std::string materialGraph;

        friend bool operator==(const MaterialSlotOverride&, const MaterialSlotOverride&) = default;
    };

    struct MeshComponent
    {
        CoreUUID mesh;
        // UINT32_MAX = external/imported mesh UUID in `mesh`.
        // 0 = quad, 1 = cube, 2 = sphere, 3 = capsule.
        uint32_t builtinGeometry {UINT32_MAX};
        glm::vec4 materialColor {1.0f};
        std::vector<MaterialSlotOverride> materialOverrides;
    };
} // namespace vultra
