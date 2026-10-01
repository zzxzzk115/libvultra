#pragma once

#include <vultra/drivers/rhi/resources.hpp>

#include <glm/glm.hpp>

#include <array>
#include <memory>
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
        glm::vec4  bounds;   // World-space center and radius; scene transforms are already baked.
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

    MeshletData buildMeshlets(const SceneData& scene, uint32_t workers = 0);

    class GpuMeshlets
    {
    public:
        GpuMeshlets(Device& device, Buffer& vertices, const SceneData& scene, uint32_t workers = 0);
        ~GpuMeshlets();
        GpuMeshlets(const GpuMeshlets&)            = delete;
        GpuMeshlets& operator=(const GpuMeshlets&) = delete;

        std::array<VriDescriptor*, 4> views {};
        std::vector<MeshletRange>     primitives;
        uint32_t                      count = 0;

    private:
        Device&                                m_Device;
        std::array<std::unique_ptr<Buffer>, 3> m_Buffers;
    };
} // namespace vultra
