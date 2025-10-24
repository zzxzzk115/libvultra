#pragma once

#include <glm/mat4x4.hpp>

#include <cstdint>

namespace vultra
{
    namespace gfx
    {
        struct MeshConstants
        {
            glm::mat4 model {1.0f};
            uint32_t  materialIndex {0};
            uint32_t  enableNormalMapping {0};
            uint32_t  paddingU0;
            uint32_t  paddingU1;
        };
        static_assert(sizeof(MeshConstants) % 16 == 0, "MeshConstants size must be multiple of 16 bytes");
    } // namespace gfx
} // namespace vultra