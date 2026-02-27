#pragma once

#include "vultra/core/engine/engine_subsystem.hpp"
#include "vultra/function/resource/gpu_resource_pool.hpp"
#include "vultra/function/services/gpu_resource_service.hpp"

namespace vultra
{
    class GpuResourceSystem final : public EngineSubsystem, public IGpuResourceService
    {
    public:
        ENGINE_SUBSYSTEM(GpuResourceSystem)

        bool onInit() override;
        void onShutdown() override;

        resource::GpuResourcePool&       pool() override { return m_Pool; }
        const resource::GpuResourcePool& pool() const override { return m_Pool; }

        uint32_t createMesh(rhi::RenderDevice& rd, const GpuMeshCreateDesc& desc) override;
        uint32_t createTexture(rhi::RenderDevice& rd, resource::GpuTexture tex) override;

    private:
        resource::GpuResourcePool m_Pool;
    };
} // namespace vultra
