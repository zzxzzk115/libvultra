#pragma once

#include "vultra/core/base/lua_annotations.hpp"

namespace vultra
{
    struct VLUA_CLASS(name = CapsuleShape, ref = ScriptCapsuleShapeRef, accessor = capsuleShape) CapsuleShapeComponent
    {
        VLUA_FIELD() float halfHeightOfCylinder {0.5f};
        VLUA_FIELD() float radius {0.25f};
    };
} // namespace vultra
