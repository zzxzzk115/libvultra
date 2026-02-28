#pragma once

#include "vultra/core/base/base.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/core/rhi/storage_buffer.hpp"
#include "vultra/function/resource/gpu_material.hpp"
#include "vultra/function/resource/gpu_mesh.hpp"
#include "vultra/function/resource/gpu_texture.hpp"
#include "vultra/function/resource/material_buffer.hpp"

#include <cstdint>
#include <cstring>
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
        // ------------------------------------------------------------
        // Geometry buffer pool (GPU-driven baseline)
        // ------------------------------------------------------------

        // Global merged geometry buffers.
        //
        // Notes:
        // - Indexed multi-draw indirect requires a single bound index buffer.
        //   This pool provides that by appending each mesh's index data into
        //   one global index buffer and recording per-mesh indexBase/indexCount.
        // - Vertex buffer is currently optional for CPU-driven compatibility;
        //   GPU-driven shaders may use per-mesh vertex buffer device addresses.
        struct GeometryBuffer
        {
            // Optional global vertex byte buffer for future fully pooled vertex pulling.
            Ref<rhi::StorageBuffer> vertexBytes {nullptr};
            uint64_t                vertexBytesAddress {0};
            uint32_t                vertexBytesUsed {0};

            // CPU mirror for deterministic (re)uploads when buffers grow.
            std::vector<uint8_t> cpuVertexBytes;

            // Global index buffer for indexed multi-draw indirect (uint32 indices).
            rhi::IndexBuffer index32;
            uint64_t         index32Address {0};
            uint32_t         indexCountUsed {0};

            // CPU mirror for deterministic (re)uploads when buffers grow.
            // Geometry is uploaded during asset import / upload, not per-frame.
            std::vector<uint32_t> cpuIndex32;

            void reset()
            {
                vertexBytes        = nullptr;
                vertexBytesAddress = 0;
                vertexBytesUsed    = 0;

                cpuVertexBytes.clear();

                index32        = {};
                index32Address = 0;
                indexCountUsed = 0;

                cpuIndex32.clear();
            }

            struct VertexAlloc
            {
                uint32_t byteOffset {0};
                uint32_t byteSize {0};
            };

            // Append raw vertex bytes into the global vertex byte buffer.
            // Returns the byte offset for this appended range.
            VertexAlloc appendVertexBytes(rhi::RenderDevice& rd, uint64_t bytes, const void* data)
            {
                VertexAlloc out;
                if (bytes == 0)
                    return out;

                const uint32_t base     = vertexBytesUsed;
                const uint64_t required = static_cast<uint64_t>(vertexBytesUsed) + bytes;

                out.byteOffset = base;
                out.byteSize   = static_cast<uint32_t>(bytes);

                // Append into CPU mirror
                const auto oldSize = cpuVertexBytes.size();
                cpuVertexBytes.resize(oldSize + static_cast<size_t>(bytes));
                std::memcpy(cpuVertexBytes.data() + oldSize, data, static_cast<size_t>(bytes));

                bool grew = false;

                if (!vertexBytes || static_cast<uint64_t>(vertexBytes->getSize()) < required)
                {
                    const uint64_t oldCap = vertexBytes ? static_cast<uint64_t>(vertexBytes->getSize()) : 0;
                    uint64_t       newCap = oldCap == 0 ? 256ull * 1024ull : (oldCap * 2ull);
                    if (newCap < required)
                        newCap = required;

                    vertexBytes        = createRef<rhi::StorageBuffer>(rd.createStorageBuffer(newCap));
                    vertexBytesAddress = rd.getBufferDeviceAddress(*vertexBytes);
                    grew               = true;
                }

                if (grew)
                {
                    rd.uploadS(*vertexBytes, 0, static_cast<uint64_t>(required), cpuVertexBytes.data());
                }
                else
                {
                    rd.uploadS(*vertexBytes, base, static_cast<uint64_t>(bytes), data);
                }

                vertexBytesUsed = static_cast<uint32_t>(required);
                return out;
            }

            // Append indices into the global index buffer.
            // Returns the base index (firstIndex) for this appended range.
            uint32_t appendIndices(rhi::RenderDevice& rd, const uint32_t* indices, uint32_t count)
            {
                if (count == 0)
                    return indexCountUsed;

                const uint32_t base          = indexCountUsed;
                const uint32_t requiredCount = indexCountUsed + count;

                // Append into CPU mirror.
                cpuIndex32.insert(cpuIndex32.end(), indices, indices + count);

                // Grow if needed.
                const size_t requiredBytes = static_cast<size_t>(requiredCount) * sizeof(uint32_t);
                bool         grew          = false;

                if (!index32 || index32.getSize() < requiredBytes)
                {
                    // Growth policy: double, minimum 1024 indices.
                    const uint32_t oldCap = static_cast<uint32_t>(index32 ? (index32.getSize() / sizeof(uint32_t)) : 0);
                    uint32_t       newCap = oldCap == 0 ? 1024u : (oldCap * 2u);
                    if (newCap < requiredCount)
                        newCap = requiredCount;

                    index32        = rd.createIndexBuffer(rhi::IndexType::eUInt32, newCap);
                    index32Address = rd.getBufferDeviceAddress(index32);
                    grew           = true;
                }

                // Upload data.
                // If the buffer grew, re-upload the full CPU mirror (simple, deterministic).
                // Otherwise, upload only the appended range.
                if (grew)
                {
                    const size_t bytes = static_cast<size_t>(requiredCount) * sizeof(uint32_t);
                    rd.uploadS(index32, 0, bytes, cpuIndex32.data());
                }
                else
                {
                    const size_t bytes    = static_cast<size_t>(count) * sizeof(uint32_t);
                    const size_t dstBytes = static_cast<size_t>(base) * sizeof(uint32_t);
                    rd.uploadS(index32, dstBytes, bytes, indices);
                }

                indexCountUsed = requiredCount;
                return base;
            }
        } geometry;

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
        bool                    materialTableDirty {false};

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
            geometry.reset();
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
            if (!materialTableDirty)
                return;

            const size_t bytes = materials.size() * sizeof(GpuMaterial);
            if (bytes == 0)
            {
                materialTableBuffer = nullptr;
                materialTableDirty  = false;
                return;
            }

            if (!materialTableBuffer || materialTableBuffer->getSize() < bytes)
            {
                materialTableBuffer = createRef<rhi::StorageBuffer>(rd.createStorageBuffer(bytes));
            }

            rd.uploadS(*materialTableBuffer, 0, bytes, materials.data());
            materialTableDirty = false;
        }
    };
} // namespace vultra::resource
