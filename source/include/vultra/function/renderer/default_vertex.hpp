#pragma once

#include "vultra/core/base/base.hpp"

#include <glm/glm.hpp>

#if defined(_MSC_VER)
#define PACKED_STRUCT(definition) __pragma(pack(push, 1)) definition __pragma(pack(pop))
#elif defined(__GNUC__) || defined(__clang__)
#define PACKED_STRUCT(definition) definition __attribute__((packed))
#else
#error "Unknown compiler, please define PACKED_STRUCT for it."
#endif

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
    } // namespace gfx
} // namespace vultra