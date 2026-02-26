#pragma once

#include "vultra/core/base/base.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/core/rhi/storage_buffer.hpp"
#include "vultra/function/resource/gpu_draw.hpp"
#include "vultra/function/resource/gpu_instance.hpp"
#include "vultra/function/resource/gpu_material.hpp"
#include "vultra/function/resource/gpu_mesh.hpp"
#include "vultra/function/resource/gpu_texture.hpp"
#include "vultra/function/resource/material_buffer.hpp"

#include <cstdint>
#include <vector>

namespace vultra::resource
{
    // Simple scene-side GPU resource tables.
    struct GpuScene
    {
        // Global material parameter pool (GPU buffer + CPU mirror).
        // Materials store offsets into this buffer.
        MaterialBuffer materialParams;

        // Bindless textures: stable slot indices.
        // Slot 0 is reserved for fallback.
        // NOTE: Do NOT erase/shrink this vector during runtime.
        std::vector<GpuTexture>  textures;
        std::vector<GpuMaterial> materials; // material table
        std::vector<GpuMesh>     meshes;    // mesh table

        // GPU-driven instance table.
        std::vector<GpuInstance> instances;
        Ref<rhi::StorageBuffer>  instanceBuffer {nullptr};

        // GPU-driven draw table (gl_DrawID indexed).
        // This is uploaded as an SSBO and consumed by built-in GPU-driven shaders.
        std::vector<GpuDrawRecord> draws;
        Ref<rhi::StorageBuffer>    drawBuffer {nullptr};

        // Material table buffer (GpuMaterial array).
        // The shader-side MaterialEntry layout is a compact view derived from this.
        Ref<rhi::StorageBuffer> materialTableBuffer {nullptr};

        struct PendingTextureFree
        {
            uint32_t index {0};
            uint64_t retireFrame {0};
        };

        // Free-list for reusing bindless slots.
        // Slots are only pushed here after they are safe to reuse.
        std::vector<uint32_t>           freeTextureSlots;
        std::vector<PendingTextureFree> pendingTextureFrees;

        // Ensure bindless table has a valid slot 0 reserved as fallback.
        // Slot 0 may be a null texture placeholder; the renderer can bind a real 1x1 white later.
        void ensureBindlessSlot0()
        {
            if (textures.empty())
            {
                GpuTexture fallback;
                fallback.texture       = nullptr;
                fallback.bindlessIndex = 0;
                textures.push_back(fallback);
            }
        }

        // Allocates a stable bindless slot for the given texture.
        // Returns the bindless index (stable slot id).
        uint32_t addTexture(GpuTexture tex)
        {
            ensureBindlessSlot0();

            uint32_t index = 0;
            if (!freeTextureSlots.empty())
            {
                index = freeTextureSlots.back();
                freeTextureSlots.pop_back();
                tex.bindlessIndex = index;
                textures[index]   = std::move(tex);
            }
            else
            {
                index             = static_cast<uint32_t>(textures.size());
                tex.bindlessIndex = index;
                textures.push_back(std::move(tex));
            }
            return index;
        }

        // Schedules a bindless slot to be freed (and later reused).
        // IMPORTANT: This does NOT change any existing indices stored in materials/instances.
        // Instead, the slot content should be replaced by a fallback texture, and the slot
        // becomes reusable after retireFrame.
        void scheduleFreeTextureSlot(uint32_t index, uint64_t retireFrame)
        {
            if (index == 0 || index >= textures.size())
                return;

            // Replace with fallback immediately to avoid sampling freed resources.
            // (The actual GPU resource should be destroyed by the deferred destruction queue.)
            textures[index].texture = nullptr;

            pendingTextureFrees.push_back(PendingTextureFree {index, retireFrame});
        }

        // Call once per frame (main thread) to recycle safe slots.
        void processDeferredFrees(uint64_t frameIndex)
        {
            if (pendingTextureFrees.empty())
                return;

            size_t out = 0;
            for (auto p : pendingTextureFrees)
            {
                if (frameIndex >= p.retireFrame)
                {
                    freeTextureSlots.push_back(p.index);
                }
                else
                {
                    pendingTextureFrees[out++] = p;
                }
            }
            pendingTextureFrees.resize(out);
        }

        void clear()
        {
            textures.clear();
            materials.clear();
            meshes.clear();
            instances.clear();
            instanceBuffer = nullptr;
            draws.clear();
            drawBuffer          = nullptr;
            materialTableBuffer = nullptr;
            materialParams.reset();
            freeTextureSlots.clear();
            pendingTextureFrees.clear();
        }

        // Appends an instance and (sync baseline) uploads the full instance table.
        // Later, this becomes an append-only ring + per-frame partial update.
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

        // Appends a draw record and (sync baseline) uploads the full draw table.
        // Later: GPU culling builds indirect args + draw table.
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

        // Sync baseline upload for material table.
        // Later: partial updates / append-only tables.
        void uploadMaterialTable(rhi::RenderDevice& rd)
        {
            const size_t bytes = materials.size() * sizeof(GpuMaterial);
            if (bytes == 0)
            {
                materialTableBuffer = nullptr;
                return;
            }

            if (!materialTableBuffer || materialTableBuffer->getSize() < bytes)
            {
                materialTableBuffer = createRef<rhi::StorageBuffer>(rd.createStorageBuffer(bytes));
            }
            rd.upload(*materialTableBuffer, 0, bytes, materials.data());
        }
    };
} // namespace vultra::resource
