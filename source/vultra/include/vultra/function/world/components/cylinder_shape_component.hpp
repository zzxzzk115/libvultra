#pragma once

#include "vultra/core/base/lua_annotations.hpp"

namespace vultra
{
    struct VLUA_CLASS(name = CylinderShape, ref = ScriptCylinderShapeRef, accessor = cylinderShape) CylinderShapeComponent
    {
        VLUA_FIELD() float halfHeight {0.5f};
        VLUA_FIELD() float radius {0.5f};
    };
} // namespace vultra
