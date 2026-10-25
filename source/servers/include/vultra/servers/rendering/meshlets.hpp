#pragma once

#include <vultra/assets/meshlet_data.hpp>
#include <vultra/drivers/rhi/resources.hpp>

#include <glm/glm.hpp>

#include <array>
#include <memory>
#include <vector>

namespace vultra
{
    struct SceneData;

    class GpuMeshlets
    {
    public:
        GpuMeshlets(Device& device, Buffer& vertices, const MeshletData& data);
#ifdef VULTRA_WITH_SCENE_PREPARATION
        GpuMeshlets(Device& device, Buffer& vertices, const SceneData& scene, uint32_t workers = 0);
#endif
        ~GpuMeshlets();
        GpuMeshlets(const GpuMeshlets&)                         = delete;
        GpuMeshlets&              operator=(const GpuMeshlets&) = delete;
        std::array<VriBuffer*, 3> buffers() const; // Borrowed handles for allocator telemetry.

        std::array<VriDescriptor*, 4> views {};
        std::vector<MeshletRange>     primitives;
        uint32_t                      count = 0;

    private:
        Device&                                m_Device;
        std::array<std::unique_ptr<Buffer>, 3> m_Buffers;
    };
} // namespace vultra
