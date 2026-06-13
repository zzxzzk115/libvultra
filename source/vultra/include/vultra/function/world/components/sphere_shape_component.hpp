#pragma once

#include "vultra/core/base/lua_annotations.hpp"

namespace vultra
{
    struct VLUA_CLASS(name = SphereShape, ref = ScriptSphereShapeRef, accessor = sphereShape) SphereShapeComponent
    {
        VLUA_FIELD() float radius {0.5f};
    };
} // namespace vultra
