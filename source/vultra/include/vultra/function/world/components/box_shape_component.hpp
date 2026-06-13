#pragma once

#include "vultra/core/base/lua_annotations.hpp"

#include <glm/vec3.hpp>

namespace vultra
{
    struct VLUA_CLASS(name = BoxShape, ref = ScriptBoxShapeRef, accessor = boxShape) BoxShapeComponent
    {
        VLUA_FIELD() glm::vec3 halfExtents {0.5f};
    };
} // namespace vultra
