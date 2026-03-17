#pragma once

#include "vultra/core/base/base.hpp"
#include "vultra/core/rhi/draw_indirect_buffer.hpp"
#include "vultra/core/rhi/draw_indirect_command.hpp"
#include "vultra/core/rhi/draw_indirect_type.hpp"
#include "vultra/core/rhi/radix_sorter.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/core/rhi/storage_buffer.hpp"
#include "vultra/function/resource/gpu_draw.hpp"
#include "vultra/function/resource/gpu_scene_database.hpp"
#include "vultra/function/resource/gpu_visible_meshlet.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace vultra::rhi
{
    class CommandBuffer;
}

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
    // - CPU-driven fallback staging (meshlet and gaussian-splat draws)
    // - GPU-driven transient buffers (meshlet cull/output + per-primitive draw/indirect targets)
    // - Reference to a GpuSceneDatabase that owns scene-level tables/resources
    struct GpuSceneView
    {
        const GpuSceneDatabase* database {nullptr};
        GpuSceneBuildMode       mode {GpuSceneBuildMode::eCpuDriven};

        // CPU staging (fallback/debug/per-frame)
        // Meshlet path
        std::vector<GpuDrawRecord>            draws;
        std::vector<rhi::DrawIndirectCommand> indirectCommands;

        // Gaussian-splat path (kept separate from meshlet path)
        std::vector<GpuDrawRecord> gaussianSplatDraws;

        // Optional CPU mirror for GPU-driven intermediate visibility.
        std::vector<GpuVisibleMeshlet> visibleMeshlets;

        // GPU buffers (per-view/per-frame)
        // Meshlet path
        Ref<rhi::StorageBuffer>                visibleInstanceBuffer {nullptr};
        Ref<rhi::StorageBuffer>                visibleInstanceCountBuffer {nullptr};
        Ref<rhi::StorageBuffer>                meshletCullDispatchArgsBuffer {nullptr};
        Ref<rhi::StorageBuffer>                visibleMeshletBuffer {nullptr};
        Ref<rhi::StorageBuffer>                visibleMeshletCountBuffer {nullptr};
        Ref<rhi::StorageBuffer>                drawBuffer {nullptr};
        Ref<rhi::StorageBuffer>                drawSetBuffer {nullptr};
        std::optional<rhi::DrawIndirectBuffer> indirectBuffer;

        // Gaussian-splat path
        Ref<rhi::StorageBuffer> gaussianSplatDrawBuffer {nullptr};
        Ref<rhi::StorageBuffer> gaussianSplatSortKeysBuffer {nullptr};
        Ref<rhi::StorageBuffer> gaussianSplatSortValuesBuffer {nullptr};
        Ref<rhi::StorageBuffer> gaussianSplatSortStorageBuffer {nullptr};
        Ref<rhi::StorageBuffer> gaussianSplatProjectedBuffer {nullptr};
        Ref<rhi::StorageBuffer> gaussianSplatVisibleCountBuffer {nullptr};
        std::optional<rhi::DrawIndirectBuffer> gaussianSplatIndirectBuffer;

        uint32_t maxVisibleInstances {0};
        uint32_t maxVisibleMeshlets {0};
        uint32_t maxDraws {0};
        uint32_t maxGaussianSplatDraws {0};
        uint32_t maxGaussianSplatSortElements {0};

        void clear()
        {
            database = nullptr;
            mode     = GpuSceneBuildMode::eCpuDriven;
            draws.clear();
            indirectCommands.clear();
            gaussianSplatDraws.clear();
            visibleMeshlets.clear();
            visibleInstanceBuffer      = nullptr;
            visibleInstanceCountBuffer = nullptr;
            meshletCullDispatchArgsBuffer = nullptr;
            visibleMeshletBuffer      = nullptr;
            visibleMeshletCountBuffer = nullptr;
            drawBuffer                = nullptr;
            drawSetBuffer             = nullptr;
            indirectBuffer.reset();
            gaussianSplatDrawBuffer = nullptr;
            gaussianSplatSortKeysBuffer    = nullptr;
            gaussianSplatSortValuesBuffer  = nullptr;
            gaussianSplatSortStorageBuffer = nullptr;
            gaussianSplatProjectedBuffer   = nullptr;
            gaussianSplatVisibleCountBuffer = nullptr;
            gaussianSplatIndirectBuffer.reset();
            maxVisibleInstances = 0;
            maxVisibleMeshlets = 0;
            maxDraws           = 0;
            maxGaussianSplatDraws = 0;
            maxGaussianSplatSortElements = 0;
        }

        void beginFrame(const GpuSceneDatabase& db, GpuSceneBuildMode buildMode = GpuSceneBuildMode::eCpuDriven)
        {
            database = &db;
            mode     = buildMode;
            draws.clear();
            indirectCommands.clear();
            gaussianSplatDraws.clear();
            visibleMeshlets.clear();
            maxVisibleInstances = 0;
            maxVisibleMeshlets = 0;
            maxDraws           = 0;
            maxGaussianSplatDraws = 0;
            maxGaussianSplatSortElements = 0;
        }

        [[nodiscard]] bool isCpuDriven() const { return mode == GpuSceneBuildMode::eCpuDriven; }
        [[nodiscard]] bool isGpuDriven() const { return mode == GpuSceneBuildMode::eGpuDriven; }

        void setGpuDrivenCaps(uint32_t maxVisibleInstancesCount, uint32_t maxVisible, uint32_t maxDrawCount)
        {
            maxVisibleInstances = maxVisibleInstancesCount;
            maxVisibleMeshlets = maxVisible;
            maxDraws           = maxDrawCount;
        }

        void ensureVisibleInstanceBuffers(rhi::RenderDevice& rd)
        {
            if (maxVisibleInstances == 0)
                return;

            const uint64_t idsBytes = static_cast<uint64_t>(maxVisibleInstances) * sizeof(uint32_t);
            if (!visibleInstanceBuffer || static_cast<uint64_t>(visibleInstanceBuffer->getSize()) < idsBytes)
                visibleInstanceBuffer = createRef<rhi::StorageBuffer>(rd.createStorageBuffer(idsBytes));

            if (!visibleInstanceCountBuffer || visibleInstanceCountBuffer->getSize() < sizeof(uint32_t))
                visibleInstanceCountBuffer = createRef<rhi::StorageBuffer>(rd.createStorageBuffer(sizeof(uint32_t)));

            constexpr uint64_t kDispatchArgsBytes = sizeof(uint32_t) * 3ull;
            if (!meshletCullDispatchArgsBuffer ||
                static_cast<uint64_t>(meshletCullDispatchArgsBuffer->getSize()) < kDispatchArgsBytes)
            {
                meshletCullDispatchArgsBuffer =
                    createRef<rhi::StorageBuffer>(rd.createStorageBufferWithUsage(
                        kDispatchArgsBytes,
                        vk::BufferUsageFlagBits::eIndirectBuffer));
            }
        }

        [[nodiscard]] uint32_t getDispatchableDrawCount() const
        {
            return isGpuDriven() ? maxDraws : static_cast<uint32_t>(indirectCommands.size());
        }

        [[nodiscard]] uint32_t countMeshletDraws() const
        {
            return static_cast<uint32_t>(draws.size());
        }

        [[nodiscard]] uint32_t countGaussianSplatDraws() const
        {
            return static_cast<uint32_t>(gaussianSplatDraws.size());
        }

        uint32_t pushDraw(const GpuDrawRecord& dr)
        {
            const uint32_t index = static_cast<uint32_t>(draws.size());
            draws.push_back(dr);
            return index;
        }

        uint32_t pushDraw(GpuDrawRecord&& dr)
        {
            const uint32_t index = static_cast<uint32_t>(draws.size());
            draws.push_back(std::move(dr));
            return index;
        }

        uint32_t pushMeshletDraw(GpuDrawRecord dr)
        {
            dr.flags |= gpuDrawFlagsToMask(GpuDrawFlags::eMeshlet);
            return pushDraw(std::move(dr));
        }

        uint32_t pushGaussianSplatDraw(GpuDrawRecord dr)
        {
            dr.flags |= gpuDrawFlagsToMask(GpuDrawFlags::eGaussianSplat);
            const uint32_t index = static_cast<uint32_t>(gaussianSplatDraws.size());
            gaussianSplatDraws.push_back(std::move(dr));
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

        void ensureDrawSetBuffer(rhi::RenderDevice& rd)
        {
            // 8 counters to keep alignment and allow future queue expansion.
            constexpr uint64_t kDrawSetBytes = sizeof(uint32_t) * 8ull;
            if (!drawSetBuffer || static_cast<uint64_t>(drawSetBuffer->getSize()) < kDrawSetBytes)
                drawSetBuffer = createRef<rhi::StorageBuffer>(rd.createStorageBuffer(kDrawSetBytes));
        }

        void uploadDraws(rhi::RenderDevice& rd, rhi::CommandBuffer& cb)
        {
            ensureDrawBuffer(rd);

            const size_t drawBytes = draws.size() * sizeof(GpuDrawRecord);
            if (drawBytes > 0)
            cb.update(*drawBuffer, 0, static_cast<uint64_t>(drawBytes), draws.data());
        }

        void buildIndirectFromDraws(const GpuResourcePool& pool)
        {
            indirectCommands.clear();
            indirectCommands.reserve(draws.size());

            for (uint32_t drawId = 0; drawId < static_cast<uint32_t>(draws.size()); ++drawId)
            {
                const auto& d = draws[drawId];
                if (!gpuDrawHasFlag(d.flags, GpuDrawFlags::eMeshlet))
                    continue;
                if (d.primitiveIndex >= pool.meshlets.cpuMeshlets.size())
                    continue;
                const auto& m = pool.meshlets.cpuMeshlets[d.primitiveIndex];

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
                                          (maxDraws == 0 ? 5u : maxDraws * 5u) :
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

        void prepareGpuDrivenBuffers(rhi::RenderDevice& rd,
                                     uint32_t maxVisibleInstancesCount,
                                     uint32_t maxVisible,
                                     uint32_t maxDrawCount)
        {
            mode = GpuSceneBuildMode::eGpuDriven;
            setGpuDrivenCaps(maxVisibleInstancesCount, maxVisible, maxDrawCount);
            ensureVisibleInstanceBuffers(rd);
            ensureVisibleMeshletBuffers(rd);
            ensureDrawBuffer(rd);
            ensureDrawSetBuffer(rd);
            ensureIndirectBuffer(rd);
        }

        [[nodiscard]] uint32_t getDispatchableGaussianSplatDrawCount() const
        {
            return maxGaussianSplatDraws;
        }

        void setGaussianSplatGpuDrivenCaps(uint32_t maxDrawCount)
        {
            maxGaussianSplatDraws = maxDrawCount;
        }

        void ensureGaussianSplatDrawBuffer(rhi::RenderDevice& rd)
        {
            const uint32_t count = isGpuDriven() ? maxGaussianSplatDraws : static_cast<uint32_t>(gaussianSplatDraws.size());
            const uint64_t bytes = static_cast<uint64_t>(count) * sizeof(GpuDrawRecord);
            if (bytes == 0)
                return;

            if (!gaussianSplatDrawBuffer || static_cast<uint64_t>(gaussianSplatDrawBuffer->getSize()) < bytes)
                gaussianSplatDrawBuffer = createRef<rhi::StorageBuffer>(rd.createStorageBuffer(bytes));
        }

        void uploadGaussianSplatDraws(rhi::RenderDevice& rd, rhi::CommandBuffer& cb)
        {
            ensureGaussianSplatDrawBuffer(rd);

            const size_t drawBytes = gaussianSplatDraws.size() * sizeof(GpuDrawRecord);
            if (drawBytes > 0)
            cb.update(*gaussianSplatDrawBuffer, 0, static_cast<uint64_t>(drawBytes), gaussianSplatDraws.data());
        }

        void prepareGaussianSplatGpuDrivenBuffers(rhi::RenderDevice& rd, uint32_t maxDrawCount)
        {
            mode = GpuSceneBuildMode::eGpuDriven;
            setGaussianSplatGpuDrivenCaps(maxDrawCount);
            ensureGaussianSplatDrawBuffer(rd);
            ensureGaussianSplatVisibleCountBuffer(rd);
            ensureGaussianSplatIndirectBuffer(rd);
        }

        void ensureGaussianSplatVisibleCountBuffer(rhi::RenderDevice& rd)
        {
            if (!gaussianSplatVisibleCountBuffer || gaussianSplatVisibleCountBuffer->getSize() < sizeof(uint32_t))
                gaussianSplatVisibleCountBuffer = createRef<rhi::StorageBuffer>(rd.createStorageBuffer(sizeof(uint32_t)));
        }

        void ensureGaussianSplatIndirectBuffer(rhi::RenderDevice& rd)
        {
            if (!gaussianSplatIndirectBuffer.has_value() ||
                gaussianSplatIndirectBuffer->getDrawIndirectType() != rhi::DrawIndirectType::eNonIndexed ||
                gaussianSplatIndirectBuffer->getCapacity() < 1u)
            {
                gaussianSplatIndirectBuffer = rd.createDrawIndirectBuffer(1u, rhi::DrawIndirectType::eNonIndexed);
            }
        }

        void ensureGaussianSplatSortBuffers(rhi::RenderDevice& rd,
                                            const rhi::RadixSorter& sorter,
                                            const uint32_t maxElementCount)
        {
            if (!sorter || maxElementCount == 0u)
                return;

            const uint64_t keyValueBytes = static_cast<uint64_t>(maxElementCount) * sizeof(uint32_t);

            if (!gaussianSplatSortKeysBuffer || static_cast<uint64_t>(gaussianSplatSortKeysBuffer->getSize()) < keyValueBytes)
            {
                gaussianSplatSortKeysBuffer = createRef<rhi::StorageBuffer>(
                    rd.createStorageBufferWithUsage(keyValueBytes,
                                                    sorter.getKeyValueStorageRequirements().usage));
            }

            if (!gaussianSplatSortValuesBuffer ||
                static_cast<uint64_t>(gaussianSplatSortValuesBuffer->getSize()) < keyValueBytes)
            {
                gaussianSplatSortValuesBuffer = createRef<rhi::StorageBuffer>(
                    rd.createStorageBufferWithUsage(keyValueBytes,
                                                    sorter.getKeyValueStorageRequirements().usage));
            }

            const auto storageReq = sorter.getKeyValueStorageRequirements();
            if (!gaussianSplatSortStorageBuffer ||
                static_cast<uint64_t>(gaussianSplatSortStorageBuffer->getSize()) < storageReq.size)
            {
                gaussianSplatSortStorageBuffer = createRef<rhi::StorageBuffer>(
                    rd.createStorageBufferWithUsage(storageReq.size, storageReq.usage));
            }

            constexpr uint64_t kProjectedStrideBytes = sizeof(float) * 12ull;
            const uint64_t projectedBytes = static_cast<uint64_t>(maxElementCount) * kProjectedStrideBytes;
            if (!gaussianSplatProjectedBuffer ||
                static_cast<uint64_t>(gaussianSplatProjectedBuffer->getSize()) < projectedBytes)
            {
                gaussianSplatProjectedBuffer = createRef<rhi::StorageBuffer>(
                    rd.createStorageBuffer(projectedBytes));
            }

            maxGaussianSplatSortElements = maxElementCount;
        }
    };
} // namespace vultra::resource
