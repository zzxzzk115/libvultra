#pragma once

#include "vultra/core/base/lua_annotations.hpp"
#include "vultra/core/base/uuid.hpp"

#include <glm/vec3.hpp>

#include <cstdint>

namespace vultra
{
    struct VLUA_CLASS(name = ReflectionProbe, ref = ScriptReflectionProbeRef, accessor = reflectionProbe)
        ReflectionProbeComponent
    {
        VLUA_FIELD() bool     active {true};
        VLUA_FIELD() bool     enableIBL {true};
        VLUA_FIELD() CoreUUID environmentMap;
        // 0 = box, 1 = sphere.
        VLUA_FIELD() uint32_t  shape {0};
        VLUA_FIELD() glm::vec3 boxSize {10.0f};
        VLUA_FIELD() float     radius {5.0f};
        VLUA_FIELD() float     blendDistance {1.0f};
        VLUA_FIELD() float     intensity {1.0f};
        VLUA_FIELD() int       priority {0};
        VLUA_FIELD() bool      parallaxCorrection {true};
    };
} // namespace vultra
