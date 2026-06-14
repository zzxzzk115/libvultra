#pragma once

#include "vultra/core/base/script_annotations.hpp"

#include <glm/vec3.hpp>

namespace vultra
{
    struct VBIND_USERTYPE(name = BoxShape, handle = ScriptBoxShapeRef, accessor = boxShape) BoxShapeComponent
    {
        VBIND_FIELD() glm::vec3 halfExtents {0.5f};
    };
} // namespace vultra
