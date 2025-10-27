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

        struct GlobalMeshletDataPushConstants
        {
            uint64_t vertexBufferAddress;
            uint64_t meshletBufferAddress;
            uint64_t meshletVertexBufferAddress;
            uint64_t meshletTriangleBufferAddress;

            uint32_t meshletCount;
            uint32_t enableNormalMapping;
            uint32_t debugMode;
            uint32_t padding1;

            glm::mat4 modelMatrix;
        };
        static_assert(sizeof(GlobalMeshletDataPushConstants) % 16 == 0,
                      "GlobalMeshletDataPushConstants size must be multiple of 16 bytes");
    } // namespace gfx
} // namespace vultra