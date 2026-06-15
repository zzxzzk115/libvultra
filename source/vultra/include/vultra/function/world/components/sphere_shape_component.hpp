#pragma once

#include "vultra/core/base/script_annotations.hpp"

namespace vultra
{
    struct VBIND_USERTYPE(name = SphereShape, handle = ScriptSphereShapeRef) SphereShapeComponent
    {
        VBIND_FIELD() float radius {0.5f};
    };
} // namespace vultra
