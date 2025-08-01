#pragma once

#include "vultra/core/base/base.hpp"

#include <glm/glm.hpp>

namespace vultra
{
    namespace gfx
    {
        class VertexFormat;

        struct SimpleVertex
        {
            glm::vec3 position;
            glm::vec3 normal;
            glm::vec2 texCoord;

            static Ref<VertexFormat> getVertexFormat();
        };
    } // namespace gfx
} // namespace vultra