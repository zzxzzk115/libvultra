#pragma once

#include "vultra/core/base/base.hpp"
#include "vultra/core/rhi/index_buffer.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/core/rhi/vertex_attributes.hpp"
#include "vultra/function/resource/gpu_resource_pool.hpp"
#include "vultra/function/resource/gpu_texture.hpp"

#include <vbase/core/scoped_enum_flags.hpp>
#include <vbase/service/service_registry.hpp>

#include <cstdint>

namespace vultra
{
    enum class GpuMeshUsageFlags : uint32_t
    {
        eNone      = 0,
        eCpuDriven = BIT(0),
        eGpuDriven = BIT(1),

        eAll = eCpuDriven | eGpuDriven,
    };

    struct GpuMeshCreateDesc
    {
        const void* vertexData {nullptr};
        uint64_t    vertexDataBytes {0};
        uint32_t    vertexCount {0};

        rhi::VertexAttributes vertexAttributes;
        uint32_t              vertexStrideBytes {0};

        const void*    indexData {nullptr};
        uint32_t       indexCount {0};
        rhi::IndexType indexType {rhi::IndexType::eUInt32};

        GpuMeshUsageFlags usage {GpuMeshUsageFlags::eAll};
    };

    class IGpuResourceService
    {
    public:
        SERVICE_REGISTER(IGpuResourceService)
        virtual ~IGpuResourceService() = default;

        virtual resource::GpuResourcePool&       pool()       = 0;
        virtual const resource::GpuResourcePool& pool() const = 0;

        virtual uint32_t createMesh(rhi::RenderDevice& rd, const GpuMeshCreateDesc& desc) = 0;
        virtual uint32_t createTexture(rhi::RenderDevice& rd, resource::GpuTexture tex)   = 0;
    };
} // namespace vultra

template<>
struct HasFlags<vultra::GpuMeshUsageFlags> : std::true_type
{};
