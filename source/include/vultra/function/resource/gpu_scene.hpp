#pragma once

#include "vultra/core/base/base.hpp"
#include "vultra/core/rhi/draw_indirect_buffer.hpp"
#include "vultra/core/rhi/draw_indirect_command.hpp"
#include "vultra/core/rhi/draw_indirect_type.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/core/rhi/storage_buffer.hpp"
#include "vultra/function/resource/gpu_draw.hpp"
#include "vultra/function/resource/gpu_instance.hpp"
#include "vultra/function/resource/gpu_resource_pool.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace vultra::resource
{
    // Scene-side GPU tables.
    //
    // Responsibilities:
    // - Per-frame/per-view instance table
    // - Per-frame draw table (indexed by gl_DrawID in multi-draw)
    // - Optional indirect command buffer for drawIndirect()
    //
    // Global mesh/material/texture tables live in GpuResourcePool.
    //
    // IMPORTANT:
    // This struct is designed for "build → upload" usage (batch uploads).
    // Do NOT upload on every push, otherwise it degenerates into O(N^2) traffic.
    struct GpuScene
    {
        const GpuResourcePool* resources {nullptr};

        // ------------------------------
        // CPU staging (per-frame)
        // ------------------------------

        std::vector<GpuInstance>   instances;
        std::vector<GpuDrawRecord> draws;

        // One command per draw (multi-draw indirect).
        std::vector<rhi::DrawIndirectCommand> indirectCommands;

        // ------------------------------
        // GPU buffers (per-frame)
        // ------------------------------

        Ref<rhi::StorageBuffer> instanceBuffer {nullptr};
        Ref<rhi::StorageBuffer> drawBuffer {nullptr};

        // rhi-managed indirect buffer (stride/type handled by RenderDevice).
        // Use a pointer-like wrapper to avoid including backend/Vk types in higher layers.
        std::optional<rhi::DrawIndirectBuffer> indirectBuffer;

        void clear()
        {
            resources = nullptr;

            instances.clear();
            draws.clear();
            indirectCommands.clear();

            instanceBuffer = nullptr;
            drawBuffer     = nullptr;
            indirectBuffer.reset();
        }

        void beginFrame(const GpuResourcePool& res)
        {
            resources = &res;
            instances.clear();
            draws.clear();
            indirectCommands.clear();
        }

        uint32_t pushInstance(const GpuInstance& inst)
        {
            const uint32_t index = static_cast<uint32_t>(instances.size());
            instances.push_back(inst);
            return index;
        }

        uint32_t pushDraw(const GpuDrawRecord& dr)
        {
            const uint32_t index = static_cast<uint32_t>(draws.size());
            draws.push_back(dr);
            return index;
        }

        void ensureTableBuffers(rhi::RenderDevice& rd)
        {
            const size_t instBytes = instances.size() * sizeof(GpuInstance);
            const size_t drawBytes = draws.size() * sizeof(GpuDrawRecord);

            if (instBytes > 0)
            {
                if (!instanceBuffer || instanceBuffer->getSize() < instBytes)
                    instanceBuffer = createRef<rhi::StorageBuffer>(rd.createStorageBuffer(instBytes));
            }

            if (drawBytes > 0)
            {
                if (!drawBuffer || drawBuffer->getSize() < drawBytes)
                    drawBuffer = createRef<rhi::StorageBuffer>(rd.createStorageBuffer(drawBytes));
            }
        }

        void uploadTables(rhi::RenderDevice& rd)
        {
            ensureTableBuffers(rd);

            const size_t instBytes = instances.size() * sizeof(GpuInstance);
            if (instBytes > 0)
            {
                auto instanceStagingBuffer = rd.createStagingBuffer(instBytes, instances.data());
                rd.execute(
                    [&](auto& cb) {
                        cb.copyBuffer(instanceStagingBuffer,
                                      *instanceBuffer,
                                      vk::BufferCopy {0, 0, instanceStagingBuffer.getSize()});
                    },
                    true);
            }

            const size_t drawBytes = draws.size() * sizeof(GpuDrawRecord);
            if (drawBytes > 0)
            {
                auto drawStagingBuffer = rd.createStagingBuffer(drawBytes, draws.data());
                rd.execute(
                    [&](auto& cb) {
                        cb.copyBuffer(
                            drawStagingBuffer, *drawBuffer, vk::BufferCopy {0, 0, drawStagingBuffer.getSize()});
                    },
                    true);
            }
        }

        // Build indexed indirect commands.
        //
        // Notes:
        // - Requires a single bound index buffer (GpuResourcePool::geometry.index32).
        // - cmd.count/first map to (indexCount/firstIndex).
        // - cmd.firstInstance is set to drawId to provide a stable per-draw id.
        void buildIndirectIndexedFromDraws()
        {
            indirectCommands.clear();
            indirectCommands.reserve(draws.size());

            for (uint32_t drawId = 0; drawId < static_cast<uint32_t>(draws.size()); ++drawId)
            {
                const auto& d = draws[drawId];

                rhi::DrawIndirectCommand cmd {};
                cmd.type          = rhi::DrawIndirectType::eIndexed;
                cmd.count         = d.indexCount; // indexCount
                cmd.instanceCount = 1;
                cmd.first         = d.firstIndex; // firstIndex
                cmd.vertexOffset  = 0;
                cmd.firstInstance = drawId;

                indirectCommands.push_back(cmd);
            }
        }

        void uploadIndirect(rhi::RenderDevice& rd)
        {
            // Ensure a valid buffer even if empty.
            const uint32_t cmdCount = static_cast<uint32_t>(indirectCommands.empty() ? 1 : indirectCommands.size());

            if (!indirectBuffer.has_value() || indirectBuffer->getDrawIndirectType() != rhi::DrawIndirectType::eIndexed)
            {
                indirectBuffer = rd.createDrawIndirectBuffer(cmdCount, rhi::DrawIndirectType::eIndexed);
            }

            rd.uploadDrawIndirect(*indirectBuffer, indirectCommands);
        }
    };
} // namespace vultra::resource
