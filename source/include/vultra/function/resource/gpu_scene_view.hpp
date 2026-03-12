#pragma once

#include "vultra/core/base/base.hpp"
#include "vultra/core/rhi/draw_indirect_buffer.hpp"
#include "vultra/core/rhi/draw_indirect_command.hpp"
#include "vultra/core/rhi/draw_indirect_type.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/core/rhi/storage_buffer.hpp"
#include "vultra/function/resource/gpu_draw.hpp"
#include "vultra/function/resource/gpu_scene_database.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace vultra::resource
{
    // Per-view / per-frame GPU scene state.
    //
    // Responsibilities:
    // - Draw table consumed by graphics passes
    // - Indirect command buffer for drawIndirect()
    // - Reference to a GpuSceneDatabase that owns scene-level tables/resources
    struct GpuSceneView
    {
        const GpuSceneDatabase* database {nullptr};

        // CPU staging (per-view/per-frame)
        std::vector<GpuDrawRecord>              draws;
        std::vector<rhi::DrawIndirectCommand>   indirectCommands;

        // GPU buffers (per-view/per-frame)
        Ref<rhi::StorageBuffer>                 drawBuffer {nullptr};
        std::optional<rhi::DrawIndirectBuffer> indirectBuffer;

        void clear()
        {
            database = nullptr;
            draws.clear();
            indirectCommands.clear();
            drawBuffer = nullptr;
            indirectBuffer.reset();
        }

        void beginFrame(const GpuSceneDatabase& db)
        {
            database = &db;
            draws.clear();
            indirectCommands.clear();
        }

        uint32_t pushDraw(const GpuDrawRecord& dr)
        {
            const uint32_t index = static_cast<uint32_t>(draws.size());
            draws.push_back(dr);
            return index;
        }

        void ensureDrawBuffer(rhi::RenderDevice& rd)
        {
            const size_t drawBytes = draws.size() * sizeof(GpuDrawRecord);
            if (drawBytes == 0)
                return;

            if (!drawBuffer || drawBuffer->getSize() < drawBytes)
                drawBuffer = createRef<rhi::StorageBuffer>(rd.createStorageBuffer(drawBytes));
        }

        void uploadDraws(rhi::RenderDevice& rd)
        {
            ensureDrawBuffer(rd);

            const size_t drawBytes = draws.size() * sizeof(GpuDrawRecord);
            if (drawBytes > 0)
                rd.uploadS(*drawBuffer, 0, static_cast<uint64_t>(drawBytes), draws.data());
        }

        void buildIndirectIndexedFromDraws()
        {
            indirectCommands.clear();
            indirectCommands.reserve(draws.size());

            for (uint32_t drawId = 0; drawId < static_cast<uint32_t>(draws.size()); ++drawId)
            {
                const auto& d = draws[drawId];

                rhi::DrawIndirectCommand cmd {};
                cmd.type          = rhi::DrawIndirectType::eIndexed;
                cmd.count         = d.indexCount;
                cmd.instanceCount = 1;
                cmd.first         = d.firstIndex;
                cmd.vertexOffset  = 0;
                cmd.firstInstance = drawId; // MoltenVK-friendly drawId path

                indirectCommands.push_back(cmd);
            }
        }

        void uploadIndirect(rhi::RenderDevice& rd)
        {
            const uint32_t cmdCount = static_cast<uint32_t>(indirectCommands.empty() ? 1 : indirectCommands.size());

            if (!indirectBuffer.has_value() || indirectBuffer->getDrawIndirectType() != rhi::DrawIndirectType::eIndexed)
                indirectBuffer = rd.createDrawIndirectBuffer(cmdCount, rhi::DrawIndirectType::eIndexed);

            rd.uploadDrawIndirect(*indirectBuffer, indirectCommands);
        }
    };
} // namespace vultra::resource
