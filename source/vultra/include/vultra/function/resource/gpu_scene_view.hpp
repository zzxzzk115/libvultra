#pragma once

#include "vultra/core/base/base.hpp"
#include "vultra/core/rhi/draw_indirect_buffer.hpp"
#include "vultra/core/rhi/structs/draw_indirect_command.hpp"
#include "vultra/core/rhi/structs/draw_indirect_type.hpp"
#include "vultra/core/rhi/structs/buffer_usage.hpp"
#include "vultra/core/rhi/radix_sorter.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/core/rhi/storage_buffer.hpp"
#include "vultra/function/resource/gpu_draw.hpp"
#include "vultra/function/resource/gpu_scene_database.hpp"
#include "vultra/function/resource/gpu_visible_meshlet.hpp"

#include <cstdint>
#include <algorithm>
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
    // - CPU-driven fallback staging for meshlet draws
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

        // Unified eGeneral gaussian splat path
        std::vector<GpuGeneralGaussianSplatDrawRecord>  generalGaussianSplatDraws;
        std::vector<GpuGeneralGaussianSplatPackedSource> generalGaussianSplatPackedSources;
        std::vector<GpuGeneralGaussianSplatSelectedSource> generalGaussianSplatSelectedSources;

        // Optional CPU mirror for GPU-driven intermediate visibility.
        std::vector<GpuVisibleMeshlet> visibleMeshlets;

        // GPU buffers (per-view/per-frame)
        // Meshlet path
        Ref<rhi::StorageBuffer>                visibleInstanceBuffer {nullptr};
        Ref<rhi::StorageBuffer>                visibleInstanceCountBuffer {nullptr};
        Ref<rhi::StorageBuffer>                meshletCullDispatchArgsBuffer {nullptr};
        Ref<rhi::StorageBuffer>                visibleMeshletBuffer {nullptr};
        Ref<rhi::DrawIndirectBuffer>           visibleMeshletCountBuffer {nullptr};
        Ref<rhi::StorageBuffer>                drawBuffer {nullptr};
        Ref<rhi::DrawIndirectBuffer>           drawSetBuffer {nullptr};
        std::optional<rhi::DrawIndirectBuffer> indirectBuffer;

        // General gaussian splat path
        Ref<rhi::StorageBuffer>                generalGaussianSplatDrawBuffer {nullptr};
        Ref<rhi::StorageBuffer>                generalGaussianSplatPackedSourceBuffer {nullptr};
        Ref<rhi::StorageBuffer>                generalGaussianSplatSelectedSourceBuffer {nullptr};
        Ref<rhi::StorageBuffer>                generalGaussianSplatVisibleSplatBuffer {nullptr};
        Ref<rhi::StorageBuffer>                generalGaussianSplatSortKeyBuffer {nullptr};
        Ref<rhi::StorageBuffer>                generalGaussianSplatSortIndexBuffer {nullptr};
        Ref<rhi::StorageBuffer>                generalGaussianSplatVisibleCountBuffer {nullptr};
        Ref<rhi::StorageBuffer>                generalGaussianSplatDispatchArgsBuffer {nullptr};
        Ref<rhi::StorageBuffer>                generalGaussianSplatSortStorageBuffer {nullptr};
        Ref<rhi::StorageBuffer>                generalGaussianSplatShBuffer {nullptr};
        std::optional<rhi::DrawIndirectBuffer> generalGaussianSplatIndirectBuffer;
        std::optional<rhi::RadixSorter>        generalGaussianSplatSorter;

        uint32_t maxVisibleInstances {0};
        uint32_t maxVisibleMeshlets {0};
        uint32_t maxDraws {0};
        uint32_t maxGeneralGaussianSplatDraws {0};
        // Source count is the full packed raw table; point count below is the
        // current selected-source table length and can shrink under Ordered CLOD.
        uint32_t maxGeneralGaussianSplatSourceCount {0};
        uint32_t maxGeneralGaussianSplatPoints {0};
        uint32_t maxGeneralGaussianSplatVisibleSplats {0};

        void clear()
        {
            database = nullptr;
            mode     = GpuSceneBuildMode::eCpuDriven;
            draws.clear();
            indirectCommands.clear();
            generalGaussianSplatDraws.clear();
            generalGaussianSplatPackedSources.clear();
            generalGaussianSplatSelectedSources.clear();
            visibleMeshlets.clear();
            visibleInstanceBuffer         = nullptr;
            visibleInstanceCountBuffer    = nullptr;
            meshletCullDispatchArgsBuffer = nullptr;
            visibleMeshletBuffer          = nullptr;
            visibleMeshletCountBuffer     = nullptr;
            drawBuffer                    = nullptr;
            drawSetBuffer                 = nullptr;
            indirectBuffer.reset();
            generalGaussianSplatDrawBuffer        = nullptr;
            generalGaussianSplatPackedSourceBuffer = nullptr;
            generalGaussianSplatSelectedSourceBuffer = nullptr;
            generalGaussianSplatVisibleSplatBuffer = nullptr;
            generalGaussianSplatSortKeyBuffer      = nullptr;
            generalGaussianSplatSortIndexBuffer    = nullptr;
            generalGaussianSplatVisibleCountBuffer = nullptr;
            generalGaussianSplatDispatchArgsBuffer = nullptr;
            generalGaussianSplatSortStorageBuffer  = nullptr;
            generalGaussianSplatShBuffer           = nullptr;
            generalGaussianSplatIndirectBuffer.reset();
            generalGaussianSplatSorter.reset();
            maxVisibleInstances          = 0;
            maxVisibleMeshlets           = 0;
            maxDraws                     = 0;
            maxGeneralGaussianSplatDraws         = 0;
            maxGeneralGaussianSplatSourceCount   = 0;
            maxGeneralGaussianSplatPoints        = 0;
            maxGeneralGaussianSplatVisibleSplats = 0;
        }

        void beginFrame(const GpuSceneDatabase& db, GpuSceneBuildMode buildMode = GpuSceneBuildMode::eCpuDriven)
        {
            database = &db;
            mode     = buildMode;
            draws.clear();
            indirectCommands.clear();
            generalGaussianSplatDraws.clear();
            generalGaussianSplatPackedSources.clear();
            generalGaussianSplatSelectedSources.clear();
            visibleMeshlets.clear();
            maxVisibleInstances          = 0;
            maxVisibleMeshlets           = 0;
            maxDraws                     = 0;
            maxGeneralGaussianSplatDraws         = 0;
            maxGeneralGaussianSplatSourceCount   = 0;
            maxGeneralGaussianSplatPoints        = 0;
            maxGeneralGaussianSplatVisibleSplats = 0;
        }

        [[nodiscard]] bool isCpuDriven() const { return mode == GpuSceneBuildMode::eCpuDriven; }
        [[nodiscard]] bool isGpuDriven() const { return mode == GpuSceneBuildMode::eGpuDriven; }

        void setGpuDrivenCaps(uint32_t maxVisibleInstancesCount, uint32_t maxVisible, uint32_t maxDrawCount)
        {
            maxVisibleInstances = maxVisibleInstancesCount;
            maxVisibleMeshlets  = maxVisible;
            maxDraws            = maxDrawCount;
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
                meshletCullDispatchArgsBuffer = createRef<rhi::StorageBuffer>(
                    rd.createStorageBufferWithUsage(kDispatchArgsBytes, rhi::BufferUsage::eIndirectBuffer));
            }
        }

        [[nodiscard]] uint32_t getDispatchableDrawCount() const
        {
            return isGpuDriven() ? maxDraws : static_cast<uint32_t>(indirectCommands.size());
        }

        [[nodiscard]] uint32_t countMeshletDraws() const { return static_cast<uint32_t>(draws.size()); }
        [[nodiscard]] uint32_t countGeneralGaussianSplatDraws() const
        {
            return static_cast<uint32_t>(generalGaussianSplatDraws.size());
        }
        [[nodiscard]] bool hasGeneralGaussianSplats() const
        {
            return !generalGaussianSplatDraws.empty() || maxGeneralGaussianSplatDraws > 0u;
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

        uint32_t pushGeneralGaussianSplatDraw(const GpuGeneralGaussianSplatDrawRecord& dr)
        {
            const uint32_t index = static_cast<uint32_t>(generalGaussianSplatDraws.size());
            generalGaussianSplatDraws.push_back(dr);
            return index;
        }

        uint32_t pushGeneralGaussianSplatSource(const GpuGeneralGaussianSplatPackedSource& src)
        {
            const uint32_t index = static_cast<uint32_t>(generalGaussianSplatPackedSources.size());
            generalGaussianSplatPackedSources.push_back(src);
            return index;
        }

        uint32_t pushGeneralGaussianSplatSelectedSource(const GpuGeneralGaussianSplatSelectedSource& src)
        {
            const uint32_t index = static_cast<uint32_t>(generalGaussianSplatSelectedSources.size());
            generalGaussianSplatSelectedSources.push_back(src);
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
                visibleMeshletCountBuffer = createRef<rhi::DrawIndirectBuffer>(
                    rd.createDrawIndirectBufferBySize(sizeof(uint32_t), rhi::DrawIndirectType::eNonIndexed));
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
                drawSetBuffer = createRef<rhi::DrawIndirectBuffer>(
                    rd.createDrawIndirectBufferBySize(kDrawSetBytes, rhi::DrawIndirectType::eNonIndexed));
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
                indirectBuffer = rd.createDrawIndirectBufferByCount(cmdCount, rhi::DrawIndirectType::eNonIndexed);
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
                                     uint32_t           maxVisibleInstancesCount,
                                     uint32_t           maxVisible,
                                     uint32_t           maxDrawCount)
        {
            mode = GpuSceneBuildMode::eGpuDriven;
            setGpuDrivenCaps(maxVisibleInstancesCount, maxVisible, maxDrawCount);
            ensureVisibleInstanceBuffers(rd);
            ensureVisibleMeshletBuffers(rd);
            ensureDrawBuffer(rd);
            ensureDrawSetBuffer(rd);
            ensureIndirectBuffer(rd);
        }

        // maxSourceCount sizes immutable packed raw data, while maxPointCount sizes
        // the per-frame selected-source indirection consumed by the preprocess pass.
        void setGeneralGaussianSplatCaps(uint32_t maxDrawCount,
                                         uint32_t maxSourceCount,
                                         uint32_t maxPointCount,
                                         uint32_t maxVisibleSplatCount)
        {
            maxGeneralGaussianSplatDraws         = maxDrawCount;
            maxGeneralGaussianSplatSourceCount   = maxSourceCount;
            maxGeneralGaussianSplatPoints        = maxPointCount;
            maxGeneralGaussianSplatVisibleSplats = maxVisibleSplatCount;
        }

        void ensureGeneralGaussianSplatBuffers(rhi::RenderDevice& rd)
        {
            if (maxGeneralGaussianSplatDraws > 0u)
            {
                const uint64_t drawBytes =
                    static_cast<uint64_t>(maxGeneralGaussianSplatDraws) * sizeof(GpuGeneralGaussianSplatDrawRecord);
                if (!generalGaussianSplatDrawBuffer ||
                    static_cast<uint64_t>(generalGaussianSplatDrawBuffer->getSize()) < drawBytes)
                {
                    generalGaussianSplatDrawBuffer = createRef<rhi::StorageBuffer>(rd.createStorageBuffer(drawBytes));
                }
            }

            if (maxGeneralGaussianSplatSourceCount > 0u)
            {
                const uint64_t packedBytes =
                    static_cast<uint64_t>(maxGeneralGaussianSplatSourceCount) * sizeof(GpuGeneralGaussianSplatPackedSource);
                if (!generalGaussianSplatPackedSourceBuffer ||
                    static_cast<uint64_t>(generalGaussianSplatPackedSourceBuffer->getSize()) < packedBytes)
                {
                    generalGaussianSplatPackedSourceBuffer =
                        createRef<rhi::StorageBuffer>(rd.createStorageBuffer(packedBytes));
                }
            }

            if (maxGeneralGaussianSplatPoints > 0u)
            {
                const uint64_t selectedBytes =
                    static_cast<uint64_t>(maxGeneralGaussianSplatPoints) * sizeof(GpuGeneralGaussianSplatSelectedSource);
                if (!generalGaussianSplatSelectedSourceBuffer ||
                    static_cast<uint64_t>(generalGaussianSplatSelectedSourceBuffer->getSize()) < selectedBytes)
                {
                    generalGaussianSplatSelectedSourceBuffer =
                        createRef<rhi::StorageBuffer>(rd.createStorageBuffer(selectedBytes));
                }
            }

            if (maxGeneralGaussianSplatVisibleSplats == 0u)
                return;

            const uint64_t visibleBytes = static_cast<uint64_t>(maxGeneralGaussianSplatVisibleSplats) *
                                          sizeof(GpuGeneralGaussianSplatVisibleSplat);
            // vk_radix_sort may bind one implementation-side sentinel element for
            // key/value buffers, so keep the external sort inputs one uint32_t
            // larger than the visible splat cap.
            const uint64_t sortBytes =
                static_cast<uint64_t>(maxGeneralGaussianSplatVisibleSplats) * sizeof(uint32_t) + sizeof(uint32_t);

            if (!generalGaussianSplatVisibleSplatBuffer ||
                static_cast<uint64_t>(generalGaussianSplatVisibleSplatBuffer->getSize()) < visibleBytes)
            {
                generalGaussianSplatVisibleSplatBuffer = createRef<rhi::StorageBuffer>(rd.createStorageBuffer(visibleBytes));
            }

            if (!generalGaussianSplatSortKeyBuffer ||
                static_cast<uint64_t>(generalGaussianSplatSortKeyBuffer->getSize()) < sortBytes)
            {
                generalGaussianSplatSortKeyBuffer = createRef<rhi::StorageBuffer>(rd.createStorageBuffer(sortBytes));
            }

            if (!generalGaussianSplatSortIndexBuffer ||
                static_cast<uint64_t>(generalGaussianSplatSortIndexBuffer->getSize()) < sortBytes)
            {
                generalGaussianSplatSortIndexBuffer = createRef<rhi::StorageBuffer>(rd.createStorageBuffer(sortBytes));
            }

            if (!generalGaussianSplatVisibleCountBuffer ||
                static_cast<uint64_t>(generalGaussianSplatVisibleCountBuffer->getSize()) < sizeof(uint32_t))
            {
                generalGaussianSplatVisibleCountBuffer =
                    createRef<rhi::StorageBuffer>(rd.createStorageBuffer(sizeof(uint32_t)));
            }

            constexpr uint64_t kDispatchArgsBytes = sizeof(uint32_t) * 4ull;
            if (!generalGaussianSplatDispatchArgsBuffer ||
                static_cast<uint64_t>(generalGaussianSplatDispatchArgsBuffer->getSize()) < kDispatchArgsBytes)
            {
                generalGaussianSplatDispatchArgsBuffer = createRef<rhi::StorageBuffer>(
                    rd.createStorageBufferWithUsage(kDispatchArgsBytes, rhi::BufferUsage::eIndirectBuffer));
            }

            if (!generalGaussianSplatIndirectBuffer.has_value() || generalGaussianSplatIndirectBuffer->getCapacity() < 1u)
            {
                generalGaussianSplatIndirectBuffer =
                    rd.createDrawIndirectBufferByCount(1u, rhi::DrawIndirectType::eNonIndexed);
            }

            if (!generalGaussianSplatSorter.has_value() || !static_cast<bool>(*generalGaussianSplatSorter) ||
                generalGaussianSplatSorter->getMaxElementCount() < maxGeneralGaussianSplatVisibleSplats)
            {
                generalGaussianSplatSorter = rd.createRadixSorter(maxGeneralGaussianSplatVisibleSplats);
            }

            if (generalGaussianSplatSorter.has_value() && static_cast<bool>(*generalGaussianSplatSorter))
            {
                const auto req = generalGaussianSplatSorter->getKeyValueStorageRequirements();
                if (req.size > 0u &&
                    (!generalGaussianSplatSortStorageBuffer ||
                     static_cast<uint64_t>(generalGaussianSplatSortStorageBuffer->getSize()) < req.size))
                {
                    generalGaussianSplatSortStorageBuffer = createRef<rhi::StorageBuffer>(
                        rd.createStorageBufferWithUsage(req.size, req.usage));
                }
            }
        }

    };
} // namespace vultra::resource
