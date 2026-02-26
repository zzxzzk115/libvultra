#pragma once

#include "vultra/core/base/base.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/core/rhi/storage_buffer.hpp"
#include "vultra/function/resource/gpu_material.hpp"
#include "vultra/function/resource/gpu_mesh.hpp"
#include "vultra/function/resource/gpu_texture.hpp"
#include "vultra/function/resource/material_buffer.hpp"

#include <cstdint>
#include <vector>

namespace vultra::resource
{
    // Global GPU resource tables managed by AssetSystem.
    //
    // Responsibilities:
    // - Own GPU buffers for meshes/materials/textures
    // - Maintain stable bindless texture slot indices
    //
    // Non-responsibilities:
    // - Per-frame/per-view draw lists
    // - Instance/draw buffers (GpuScene owns those)
    struct GpuResourcePool
    {
        // Global material parameter pool (GPU buffer + CPU mirror).
        // Materials store offsets into this buffer.
        MaterialBuffer materialParams;

        // Bindless textures: stable slot indices.
        // Slot 0 is reserved for fallback.
        // NOTE: Do NOT erase/shrink this vector during runtime.
        std::vector<GpuTexture>  textures;
        std::vector<GpuMaterial> materials;
        std::vector<GpuMesh>     meshes;

        // Material table buffer (GpuMaterial array).
        // The shader-side MaterialEntry layout is a compact view derived from this.
        Ref<rhi::StorageBuffer> materialTableBuffer {nullptr};

        struct PendingTextureFree
        {
            uint32_t index {0};
            uint64_t retireFrame {0};
        };

        std::vector<uint32_t>           freeTextureSlots;
        std::vector<PendingTextureFree> pendingTextureFrees;

        void ensureBindlessSlot0(rhi::RenderDevice& rd)
        {
            if (textures.empty())
            {
                GpuTexture fallback;
                fallback.texture       = rd.createDefaultWhite1x1Texture2D();
                fallback.bindlessIndex = 0;
                textures.push_back(fallback);
            }
        }

        uint32_t addTexture(GpuTexture tex)
        {
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

        void scheduleFreeTextureSlot(uint32_t index, uint64_t retireFrame)
        {
            if (index == 0 || index >= textures.size())
                return;

            textures[index].texture = nullptr;
            pendingTextureFrees.push_back(PendingTextureFree {index, retireFrame});
        }

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
            materialTableBuffer = nullptr;
            materialParams.reset();
            freeTextureSlots.clear();
            pendingTextureFrees.clear();
        }

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
