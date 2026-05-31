#pragma once

#include "vultra/core/base/uuid.hpp"

#include <glm/mat4x4.hpp>

#include <vector>

namespace vultra
{
    struct SkinPaletteComponent
    {
        CoreUUID skeleton;
        std::vector<glm::mat4> matrices;
    };
} // namespace vultra
