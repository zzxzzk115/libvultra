#pragma once

#include "vultra/core/base/uuid.hpp"

#include <glm/vec3.hpp>

namespace vultra
{
    struct ReflectionProbeComponent
    {
        bool      active {true};
        bool      enableIBL {true};
        CoreUUID environmentMap;
        // 0 = box, 1 = sphere.
        uint32_t shape {0};
        glm::vec3 boxSize {10.0f};
        float     radius {5.0f};
        float     blendDistance {1.0f};
        float     intensity {1.0f};
        int       priority {0};
        bool      parallaxCorrection {true};
    };
} // namespace vultra
