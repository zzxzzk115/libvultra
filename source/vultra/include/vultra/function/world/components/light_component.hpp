#pragma once

#include "vultra/core/base/lua_annotations.hpp"

#include <glm/vec3.hpp>

#include <cstdint>

namespace vultra
{
    // Numeric light kind keeps .vscn v1 serialization simple:
    // 0 = directional, 1 = point, 2 = spot, 3 = rectangle area.
    struct VLUA_CLASS(name = Light, ref = ScriptLightRef, accessor = light) LightComponent
    {
        VLUA_FIELD() uint32_t kind {0};

        VLUA_FIELD() glm::vec3 color {1.0f};
        VLUA_FIELD() float     intensity {8.0f};

        VLUA_FIELD() float range {10.0f};

        VLUA_FIELD() float radius {0.05f};
        VLUA_FIELD() float width {1.0f};
        VLUA_FIELD() float height {1.0f};
        // cone half-angles: not annotated -- the "Degrees" suffix conflicts
        // with the angles-default-to-degrees rule (doc/lua_api_design.md sec.3)
        float innerConeDegrees {20.0f};
        float outerConeDegrees {30.0f};

        VLUA_FIELD() bool castsShadow {false};
        VLUA_FIELD() bool twoSided {false};
    };
} // namespace vultra
