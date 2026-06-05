#pragma once

#include "vultra/core/base/uuid.hpp"

#include <glm/vec4.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace vultra
{
    enum class MaterialPropertyBlockValueType : uint8_t
    {
        eFloat,
        eColor,
        eTexture2D,
    };

    struct MaterialPropertyBlockEntry
    {
        std::string name;
        MaterialPropertyBlockValueType type {MaterialPropertyBlockValueType::eFloat};
        float      floatValue {0.0f};
        glm::vec4  colorValue {1.0f};
        std::string textureUri;

        friend bool operator==(const MaterialPropertyBlockEntry&, const MaterialPropertyBlockEntry&) = default;
    };

    struct MaterialSlotOverride
    {
        uint32_t    slot {0};
        std::string material;
        std::string materialGraph;
        std::vector<MaterialPropertyBlockEntry> properties;

        friend bool operator==(const MaterialSlotOverride&, const MaterialSlotOverride&) = default;
    };

    struct MeshComponent
    {
        CoreUUID mesh;
        // UINT32_MAX = external/imported mesh UUID in `mesh`.
        // 0 = quad, 1 = cube, 2 = sphere, 3 = capsule.
        uint32_t builtinGeometry {UINT32_MAX};
        // Material/colour is driven entirely by materialOverrides. To tint a builtin primitive, add a
        // slot-0 override using the builtin default material with a "baseColor" colour property.
        std::vector<MaterialSlotOverride> materialOverrides;
    };
} // namespace vultra
