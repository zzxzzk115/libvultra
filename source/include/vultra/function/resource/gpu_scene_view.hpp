#pragma once

#include "vultra/core/base/base.hpp"
#include "vultra/core/rhi/draw_indirect_buffer.hpp"
#include "vultra/core/rhi/draw_indirect_command.hpp"
#include "vultra/core/rhi/draw_indirect_type.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/core/rhi/storage_buffer.hpp"
#include "vultra/function/resource/gpu_draw.hpp"
#include "vultra/function/resource/gpu_scene_database.hpp"
#include "vultra/function/resource/gpu_visible_meshlet.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace vultra::resource
{
    enum class GpuSceneBuildMode : uint8_t
    {
        eCpuDriven,
        eGpuDriven,
    };

    // Per-view / per-frame GPU scene state.
    //
    // Responsibilities:
    // - CPU-driven fallback staging (draws + indirectCommands)
    // - GPU-driven transient buffers (visible meshlets + draw/indirect targets)
    // - Reference to a GpuSceneDatabase that owns scene-level tables/resources
    struct GpuSceneView
    {
        const GpuSceneDatabase* database {nullptr};
        GpuSceneBuildMode       mode {GpuSceneBuildMode::eCpuDriven};

        // CPU staging (fallback/debug/per-frame)
        std::vector<GpuDrawRecord>            draws;
        std::vector<rhi::DrawIndirectCommand> indirectCommands;

        // Optional CPU mirror for GPU-driven intermediate visibility.
        std::vector<GpuVisibleMeshlet> visibleMeshlets;

        // GPU buffers (per-view/per-frame)
        Ref<rhi::StorageBuffer>                visibleMeshletBuffer {nullptr};
        Ref<rhi::StorageBuffer>                visibleMeshletCountBuffer {nullptr};
        Ref<rhi::StorageBuffer>                drawBuffer {nullptr};
        std::optional<rhi::DrawIndirectBuffer> indirectBuffer;

        uint32_t maxVisibleMeshlets {0};
        uint32_t maxDraws {0};

        void clear()
        {
            database = nullptr;
            mode     = GpuSceneBuildMode::eCpuDriven;
            draws.clear();
            indirectCommands.clear();
            visibleMeshlets.clear();
            visibleMeshletBuffer      = nullptr;
            visibleMeshletCountBuffer = nullptr;
            drawBuffer                = nullptr;
            indirectBuffer.reset();
            maxVisibleMeshlets = 0;
            maxDraws           = 0;
        }

        void beginFrame(const GpuSceneDatabase& db, GpuSceneBuildMode buildMode = GpuSceneBuildMode::eCpuDriven)
        {
            database = &db;
            mode     = buildMode;
            draws.clear();
            indirectCommands.clear();
            visibleMeshlets.clear();
            maxVisibleMeshlets = 0;
            maxDraws           = 0;
        }

        [[nodiscard]] bool isCpuDriven() const { return mode == GpuSceneBuildMode::eCpuDriven; }
        [[nodiscard]] bool isGpuDriven() const { return mode == GpuSceneBuildMode::eGpuDriven; }

        void setGpuDrivenCaps(uint32_t maxVisible, uint32_t maxDrawCount)
        {
            maxVisibleMeshlets = maxVisible;
            maxDraws           = maxDrawCount;
        }

        [[nodiscard]] uint32_t getDispatchableDrawCount() const
        {
            return isGpuDriven() ? maxDraws : static_cast<uint32_t>(indirectCommands.size());
        }

        uint32_t pushDraw(const GpuDrawRecord& dr)
        {
            const uint32_t index = static_cast<uint32_t>(draws.size());
            draws.push_back(dr);
            return index;
        }

        uint32_t pushVisibleMeshlet(const GpuVisibleMeshlet& vm)
        {
            const uint32_t index = static_cast<uint32_t>(visibleMeshlets.size());
            visibleMeshlets.push_back(vm);
            return index;
        }

        void ensureVisibleMeshletBuffers(rhi::RenderDevice& rd)
        {
            if (maxVisibleMeshlets == 0)
                return;

            const uint64_t idsBytes = static_cast<uint64_t>(maxVisibleMeshlets) * sizeof(GpuVisibleMeshlet);
            if (!visibleMeshletBuffer || static_cast<uint64_t>(visibleMeshletBuffer->getSize()) < idsBytes)
                visibleMeshletBuffer = createRef<rhi::StorageBuffer>(rd.createStorageBuffer(idsBytes));

            if (!visibleMeshletCountBuffer || visibleMeshletCountBuffer->getSize() < sizeof(uint32_t))
                visibleMeshletCountBuffer = createRef<rhi::StorageBuffer>(rd.createStorageBuffer(sizeof(uint32_t)));
        }

        void ensureDrawBuffer(rhi::RenderDevice& rd)
        {
            const uint32_t count = isGpuDriven() ? maxDraws : static_cast<uint32_t>(draws.size());
            const uint64_t bytes = static_cast<uint64_t>(count) * sizeof(GpuDrawRecord);
            if (bytes == 0)
                return;

            if (!drawBuffer || static_cast<uint64_t>(drawBuffer->getSize()) < bytes)
                drawBuffer = createRef<rhi::StorageBuffer>(rd.createStorageBuffer(bytes));
        }

        void uploadDraws(rhi::RenderDevice& rd)
        {
            ensureDrawBuffer(rd);

            const size_t drawBytes = draws.size() * sizeof(GpuDrawRecord);
            if (drawBytes > 0)
                rd.uploadS(*drawBuffer, 0, static_cast<uint64_t>(drawBytes), draws.data());
        }

        void buildIndirectFromDraws(const GpuResourcePool& pool)
        {
            indirectCommands.clear();
            indirectCommands.reserve(draws.size());

            for (uint32_t drawId = 0; drawId < static_cast<uint32_t>(draws.size()); ++drawId)
            {
                const auto& d = draws[drawId];
                if (d.meshletIndex >= pool.meshlets.cpuMeshlets.size())
                    continue;
                const auto& m = pool.meshlets.cpuMeshlets[d.meshletIndex];

                rhi::DrawIndirectCommand cmd {};
                cmd.type          = rhi::DrawIndirectType::eNonIndexed;
                cmd.count         = m.triangleCount * 3u;
                cmd.instanceCount = 1;
                cmd.first         = 0;
                cmd.vertexOffset  = 0;
                cmd.firstInstance = drawId; // MoltenVK-friendly drawId path

                indirectCommands.push_back(cmd);
            }

            maxDraws = static_cast<uint32_t>(indirectCommands.size());
        }

        void ensureIndirectBuffer(rhi::RenderDevice& rd)
        {
            const uint32_t cmdCount = isGpuDriven() ?
                                          (maxDraws == 0 ? 1u : maxDraws) :
                                          static_cast<uint32_t>(indirectCommands.empty() ? 1 : indirectCommands.size());

            if (!indirectBuffer.has_value() ||
                indirectBuffer->getDrawIndirectType() != rhi::DrawIndirectType::eNonIndexed ||
                indirectBuffer->getCapacity() < cmdCount)
            {
                indirectBuffer = rd.createDrawIndirectBuffer(cmdCount, rhi::DrawIndirectType::eNonIndexed);
            }
        }

        void uploadIndirect(rhi::RenderDevice& rd)
        {
            ensureIndirectBuffer(rd);
            if (!indirectBuffer.has_value())
                return;

            if (isCpuDriven())
            {
                rd.uploadDrawIndirect(*indirectBuffer, indirectCommands);
            }
        }

        void prepareGpuDrivenBuffers(rhi::RenderDevice& rd, uint32_t maxVisible, uint32_t maxDrawCount)
        {
            mode = GpuSceneBuildMode::eGpuDriven;
            setGpuDrivenCaps(maxVisible, maxDrawCount);
            ensureVisibleMeshletBuffers(rd);
            ensureDrawBuffer(rd);
            ensureIndirectBuffer(rd);
        }
    };
} // namespace vultra::resource
