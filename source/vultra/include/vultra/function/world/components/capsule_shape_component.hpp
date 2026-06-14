#pragma once

#include "vultra/core/base/script_annotations.hpp"

namespace vultra
{
    struct VBIND_USERTYPE(name = CapsuleShape, handle = ScriptCapsuleShapeRef, accessor = capsuleShape) CapsuleShapeComponent
    {
        VBIND_FIELD() float halfHeightOfCylinder {0.5f};
        VBIND_FIELD() float radius {0.25f};
    };
} // namespace vultra
