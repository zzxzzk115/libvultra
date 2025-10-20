#pragma once

#include "vultra/core/base/base.hpp"

#include <glm/glm.hpp>

namespace vultra
{
    namespace gfx
    {
        class VertexFormat;

        PACKED_STRUCT(struct SimpleVertex {
            glm::vec3 position;
            glm::vec3 color;
            glm::vec3 normal;
            glm::vec2 texCoord;
            glm::vec4 tangent;

            static Ref<VertexFormat> getVertexFormat();
        });
        static_assert(sizeof(SimpleVertex) == 60, "SimpleVertex size should be 60 bytes");
    } // namespace gfx
} // namespace vultra