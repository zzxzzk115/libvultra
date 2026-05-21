#pragma once

#include "vultra/function/services/gpu_resource_service.hpp"

#include <array>
#include <cstdint>

namespace vultra
{
    enum class BuiltinGeometryKind : uint32_t
    {
        eQuad = 0,
        eCube,
        eSphere,
        eCapsule,
    };

    class GeometryFactory
    {
    public:
        uint32_t getOrCreateMeshIndex(BuiltinGeometryKind kind, IGpuResourceService& gpuResources, rhi::RenderDevice& rd);

    private:
        uint32_t ensureUnlitMaterial(IGpuResourceService& gpuResources, rhi::RenderDevice& rd);

    private:
        uint32_t m_UnlitMaterialIndex {UINT32_MAX};
        std::array<uint32_t, 4> m_MeshIndices {
            UINT32_MAX,
            UINT32_MAX,
            UINT32_MAX,
            UINT32_MAX,
        };
    };
} // namespace vultra
