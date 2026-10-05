#pragma once

#include <cstdint>

namespace vultra
{
    enum class SceneChange
    {
        eStructure,
        eTransform,
        eMaterial,
        eCamera,
        eLighting,
        eEnvironment,
        eMetadata
    };

    // Process-local, non-consuming notifications. Each consumer keeps its own snapshot; never serialize these values.
    struct SceneChanges
    {
        uint64_t structure   = 0;
        uint64_t transforms  = 0;
        uint64_t materials   = 0;
        uint64_t camera      = 0;
        uint64_t lighting    = 0;
        uint64_t environment = 0;
        uint64_t metadata    = 0;

        void        mark(SceneChange change);
        friend bool operator==(const SceneChanges&, const SceneChanges&) = default;
    };
} // namespace vultra
