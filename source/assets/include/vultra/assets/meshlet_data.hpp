#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace vultra
{
    struct SceneData;

    // Match resources/meshlet_data.slangh and the mesh shader's output limits.
    constexpr uint32_t kMeshletVertices  = 64;
    constexpr uint32_t kMeshletTriangles = 124;
    constexpr uint32_t kMeshletTaskSize  = 32;

    struct Meshlet
    {
        glm::uvec4 geometry; // Vertex offset, triangle offset, vertex count, triangle count.
        glm::vec4  bounds;   // Scene-space center and radius; the draw transform is applied during culling.
    };

    struct MeshletRange
    {
        uint32_t first;
        uint32_t count;
    };

    struct MeshletData
    {
        std::vector<Meshlet>      meshlets;
        std::vector<uint32_t>     vertices;
        std::vector<uint32_t>     triangles; // Three local 8-bit indices packed into each uint.
        std::vector<MeshletRange> primitives;
    };

#ifdef VULTRA_WITH_MESHLET_BUILD
    MeshletData buildMeshlets(const SceneData& scene, uint32_t workers = 0);
#endif

} // namespace vultra
