#pragma once

#include "vultra/core/base/script_annotations.hpp"

namespace vultra
{
    struct VBIND_USERTYPE(name = CylinderShape, handle = ScriptCylinderShapeRef, accessor = cylinderShape) CylinderShapeComponent
    {
        VBIND_FIELD() float halfHeight {0.5f};
        VBIND_FIELD() float radius {0.5f};
    };
} // namespace vultra
