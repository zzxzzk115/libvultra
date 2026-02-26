#pragma once

#include "vultra/core/base/base.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/core/rhi/storage_buffer.hpp"
#include "vultra/function/resource/gpu_draw.hpp"
#include "vultra/function/resource/gpu_instance.hpp"
#include "vultra/function/resource/gpu_resource_pool.hpp"

#include <cstdint>
#include <vector>

namespace vultra::resource
{
    // Scene-side GPU tables.
    //
    // Responsibilities:
    // - Per-frame/per-view instance table
    // - Per-frame draw table (gl_DrawID indexed)
    // - Optional indirect command buffers
    //
    // Global mesh/material/texture tables live in GpuResourcePool.
    struct GpuScene
    {
        const GpuResourcePool* resources {nullptr};

        std::vector<GpuInstance> instances;
        Ref<rhi::StorageBuffer>  instanceBuffer {nullptr};

        std::vector<GpuDrawRecord> draws;
        Ref<rhi::StorageBuffer>    drawBuffer {nullptr};

        void clear()
        {
            instances.clear();
            draws.clear();
            instanceBuffer = nullptr;
            drawBuffer     = nullptr;
            resources      = nullptr;
        }

        uint32_t addInstance(rhi::RenderDevice& rd, const GpuInstance& inst)
        {
            const uint32_t index = static_cast<uint32_t>(instances.size());
            instances.push_back(inst);

            const size_t bytes = instances.size() * sizeof(GpuInstance);
            if (!instanceBuffer || instanceBuffer->getSize() < bytes)
            {
                instanceBuffer = createRef<rhi::StorageBuffer>(rd.createStorageBuffer(bytes));
            }
            if (bytes > 0)
            {
                rd.upload(*instanceBuffer, 0, bytes, instances.data());
            }
            return index;
        }

        uint32_t addDraw(rhi::RenderDevice& rd, const GpuDrawRecord& dr)
        {
            const uint32_t index = static_cast<uint32_t>(draws.size());
            draws.push_back(dr);

            const size_t bytes = draws.size() * sizeof(GpuDrawRecord);
            if (!drawBuffer || drawBuffer->getSize() < bytes)
            {
                drawBuffer = createRef<rhi::StorageBuffer>(rd.createStorageBuffer(bytes));
            }
            if (bytes > 0)
            {
                rd.upload(*drawBuffer, 0, bytes, draws.data());
            }
            return index;
        }
    };
} // namespace vultra::resource
