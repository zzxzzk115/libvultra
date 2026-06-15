#pragma once

#include "vultra/core/base/script_annotations.hpp"
#include "vultra/core/base/uuid.hpp"

#include <glm/vec3.hpp>

#include <cstdint>

namespace vultra
{
    struct VBIND_USERTYPE(name = ReflectionProbe, handle = ScriptReflectionProbeRef)
        ReflectionProbeComponent
    {
        VBIND_FIELD() bool     active {true};
        VBIND_FIELD() bool     enableIBL {true};
        VBIND_FIELD() CoreUUID environmentMap;
        // 0 = box, 1 = sphere.
        VBIND_FIELD() uint32_t  shape {0};
        VBIND_FIELD() glm::vec3 boxSize {10.0f};
        VBIND_FIELD() float     radius {5.0f};
        VBIND_FIELD() float     blendDistance {1.0f};
        VBIND_FIELD() float     intensity {1.0f};
        VBIND_FIELD() int       priority {0};
        VBIND_FIELD() bool      parallaxCorrection {true};
    };
} // namespace vultra
