#pragma once

#include "vultra/core/base/base.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/core/rhi/storage_buffer.hpp"
#include "vultra/function/resource/gpu_instance.hpp"
#include "vultra/function/resource/gpu_resource_pool.hpp"

#include <cstdint>
#include <vector>

namespace vultra::resource
{
    // Persistent-ish GPU scene database.
    //
    // Responsibilities:
    // - Stable pointer to the global GPU resource pool
    // - Scene/instance tables uploaded for the current cooked world snapshot
    // - Long-lived semantic owner of GPU scene data referenced by one or more views
    //
    // Notes:
    // - The current renderer still rebuilds this every frame from RenderWorldCooker.
    //   The important change is architectural: instance tables now live in the
    //   database layer instead of being mixed together with draw/indirect view data.
    struct GpuSceneDatabase
    {
        const GpuResourcePool* resources {nullptr};

        // CPU staging (scene snapshot)
        std::vector<GpuInstance> instances;

        // GPU buffers
        Ref<rhi::StorageBuffer> instanceBuffer {nullptr};

        void clear()
        {
            resources = nullptr;
            instances.clear();
            instanceBuffer = nullptr;
        }

        void beginFrame(const GpuResourcePool& res)
        {
            resources = &res;
            instances.clear();
        }

        uint32_t pushInstance(const GpuInstance& inst)
        {
            const uint32_t index = static_cast<uint32_t>(instances.size());
            instances.push_back(inst);
            return index;
        }

        void ensureInstanceBuffer(rhi::RenderDevice& rd)
        {
            const size_t instBytes = instances.size() * sizeof(GpuInstance);
            if (instBytes == 0)
                return;

            if (!instanceBuffer || instanceBuffer->getSize() < instBytes)
                instanceBuffer = createRef<rhi::StorageBuffer>(rd.createStorageBuffer(instBytes));
        }

        void uploadInstances(rhi::RenderDevice& rd)
        {
            ensureInstanceBuffer(rd);

            const size_t instBytes = instances.size() * sizeof(GpuInstance);
            if (instBytes > 0)
                rd.uploadS(*instanceBuffer, 0, static_cast<uint64_t>(instBytes), instances.data());
        }
    };
} // namespace vultra::resource
