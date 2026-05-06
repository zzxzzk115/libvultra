#pragma once

#include "vultra/core/base/uuid.hpp"

#include <string>

namespace vultra
{
    // Marks an entity subtree instantiated from a prefab scene (.vscn).
    // Currently stores URI/path to the prefab source scene.
    struct PrefabInstanceComponent
    {
        std::string prefabUri;

        // Optional: stable prefab asset id (future).
        CoreUUID prefabId;
    };
} // namespace vultra
