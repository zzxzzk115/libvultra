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

                index32        = {};
                index32Address = 0;
                indexCountUsed = 0;

                cpuIndex32.clear();
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
                    const size_t bytes         = static_cast<size_t>(requiredCount) * sizeof(uint32_t);
                    auto         stagingBuffer = rd.createStagingBuffer(bytes, cpuIndex32.data());
                    rd.execute(
                        [&](rhi::CommandBuffer& cb) {
                            cb.copyBuffer(stagingBuffer, index32, vk::BufferCopy {0, 0, bytes});
                        },
                        true);
                }
                else
                {
                    const size_t bytes         = static_cast<size_t>(count) * sizeof(uint32_t);
                    const size_t dstBytes      = static_cast<size_t>(base) * sizeof(uint32_t);
                    auto         stagingBuffer = rd.createStagingBuffer(bytes, indices);
                    rd.execute(
                        [&](rhi::CommandBuffer& cb) {
                            cb.copyBuffer(stagingBuffer, index32, vk::BufferCopy {0, dstBytes, bytes});
                        },
                        true);
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

            auto stagingBuffer = rd.createStagingBuffer(bytes, materials.data());

            rd.execute(
                [&](rhi::CommandBuffer& cb) {
                    cb.copyBuffer(stagingBuffer, *materialTableBuffer, vk::BufferCopy {0, 0, bytes});
                },
                true);
        }
    };
} // namespace vultra::resource
