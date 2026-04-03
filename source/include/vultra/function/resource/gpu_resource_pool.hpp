#pragma once

#include "vultra/core/base/base.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/core/rhi/storage_buffer.hpp"
#include "vultra/function/resource/gpu_gaussian_splat.hpp"
#include "vultra/function/resource/gpu_material.hpp"
#include "vultra/function/resource/gpu_mesh.hpp"
#include "vultra/function/resource/gpu_texture.hpp"
#include "vultra/function/resource/material_buffer.hpp"

#include <algorithm>
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
    // - Scene/view transient buffers (GpuSceneDatabase / GpuSceneView own those)
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
            // Global vertex buffer for gpu-driven rendering.
            Ref<rhi::StorageBuffer> vertexBytes {nullptr};
            rhi::DeviceAddress      vertexBytesAddress {};
            uint32_t                vertexBytesUsed {0};

            // CPU mirror for deterministic (re)uploads when buffers grow.
            std::vector<uint8_t> cpuVertexBytes;

            // Global index buffer for indexed multi-draw indirect (uint32 indices).
            rhi::IndexBuffer index32;
            rhi::DeviceAddress index32Address {};
            uint32_t         indexCountUsed {0};

            // CPU mirror for deterministic (re)uploads when buffers grow.
            // Geometry is uploaded during asset import / upload, not per-frame.
            std::vector<uint32_t> cpuIndex32;

            void reset()
            {
                vertexBytes        = nullptr;
                vertexBytesAddress = {};
                vertexBytesUsed    = 0;

                cpuVertexBytes.clear();

                index32        = {};
                index32Address = {};
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

        struct MeshletBuffers
        {
            Ref<rhi::StorageBuffer> meshletsBuffer {nullptr};
            rhi::DeviceAddress      meshletsAddress {};
            std::vector<GpuMeshlet> cpuMeshlets;

            Ref<rhi::StorageBuffer> meshletVerticesBuffer {nullptr};
            rhi::DeviceAddress      meshletVerticesAddress {};
            std::vector<uint32_t>   cpuMeshletVertices;

            Ref<rhi::StorageBuffer> meshletTrianglesBuffer {nullptr};
            rhi::DeviceAddress      meshletTrianglesAddress {};
            std::vector<uint32_t>   cpuMeshletTriangles;

            void reset()
            {
                meshletsBuffer  = nullptr;
                meshletsAddress = {};
                cpuMeshlets.clear();
                meshletVerticesBuffer  = nullptr;
                meshletVerticesAddress = {};
                cpuMeshletVertices.clear();
                meshletTrianglesBuffer  = nullptr;
                meshletTrianglesAddress = {};
                cpuMeshletTriangles.clear();
            }

            uint32_t appendMeshlets(rhi::RenderDevice& rd, const GpuMeshlet* data, uint32_t count)
            {
                if (!data || count == 0)
                    return static_cast<uint32_t>(cpuMeshlets.size());

                const uint32_t base = static_cast<uint32_t>(cpuMeshlets.size());
                cpuMeshlets.insert(cpuMeshlets.end(), data, data + count);
                const uint64_t requiredBytes = cpuMeshlets.size() * sizeof(GpuMeshlet);
                bool           grew          = false;
                if (!meshletsBuffer || static_cast<uint64_t>(meshletsBuffer->getSize()) < requiredBytes)
                {
                    uint64_t oldCap = meshletsBuffer ? static_cast<uint64_t>(meshletsBuffer->getSize()) : 0;
                    uint64_t newCap = oldCap == 0 ? 64ull * 1024ull : oldCap * 2ull;
                    if (newCap < requiredBytes)
                        newCap = requiredBytes;
                    meshletsBuffer  = createRef<rhi::StorageBuffer>(rd.createStorageBuffer(newCap));
                    meshletsAddress = rd.getBufferDeviceAddress(*meshletsBuffer);
                    grew            = true;
                }
                if (grew)
                    rd.uploadS(*meshletsBuffer, 0, requiredBytes, cpuMeshlets.data());
                else
                    rd.uploadS(*meshletsBuffer,
                               static_cast<uint64_t>(base) * sizeof(GpuMeshlet),
                               static_cast<uint64_t>(count) * sizeof(GpuMeshlet),
                               data);
                return base;
            }

            uint32_t appendMeshletVertices(rhi::RenderDevice& rd, const uint32_t* data, uint32_t count)
            {
                if (!data || count == 0)
                    return static_cast<uint32_t>(cpuMeshletVertices.size());
                const uint32_t base = static_cast<uint32_t>(cpuMeshletVertices.size());
                cpuMeshletVertices.insert(cpuMeshletVertices.end(), data, data + count);
                const uint64_t requiredBytes = cpuMeshletVertices.size() * sizeof(uint32_t);
                bool           grew          = false;
                if (!meshletVerticesBuffer || static_cast<uint64_t>(meshletVerticesBuffer->getSize()) < requiredBytes)
                {
                    uint64_t oldCap =
                        meshletVerticesBuffer ? static_cast<uint64_t>(meshletVerticesBuffer->getSize()) : 0;
                    uint64_t newCap = oldCap == 0 ? 64ull * 1024ull : oldCap * 2ull;
                    if (newCap < requiredBytes)
                        newCap = requiredBytes;
                    meshletVerticesBuffer  = createRef<rhi::StorageBuffer>(rd.createStorageBuffer(newCap));
                    meshletVerticesAddress = rd.getBufferDeviceAddress(*meshletVerticesBuffer);
                    grew                   = true;
                }
                if (grew)
                    rd.uploadS(*meshletVerticesBuffer, 0, requiredBytes, cpuMeshletVertices.data());
                else
                    rd.uploadS(*meshletVerticesBuffer,
                               static_cast<uint64_t>(base) * sizeof(uint32_t),
                               static_cast<uint64_t>(count) * sizeof(uint32_t),
                               data);
                return base;
            }

            uint32_t appendMeshletTriangles(rhi::RenderDevice& rd, const uint32_t* data, uint32_t count)
            {
                if (!data || count == 0)
                    return static_cast<uint32_t>(cpuMeshletTriangles.size());
                const uint32_t base = static_cast<uint32_t>(cpuMeshletTriangles.size());
                cpuMeshletTriangles.insert(cpuMeshletTriangles.end(), data, data + count);
                const uint64_t requiredBytes = cpuMeshletTriangles.size() * sizeof(uint32_t);
                bool           grew          = false;
                if (!meshletTrianglesBuffer || static_cast<uint64_t>(meshletTrianglesBuffer->getSize()) < requiredBytes)
                {
                    uint64_t oldCap =
                        meshletTrianglesBuffer ? static_cast<uint64_t>(meshletTrianglesBuffer->getSize()) : 0;
                    uint64_t newCap = oldCap == 0 ? 64ull * 1024ull : oldCap * 2ull;
                    if (newCap < requiredBytes)
                        newCap = requiredBytes;
                    meshletTrianglesBuffer  = createRef<rhi::StorageBuffer>(rd.createStorageBuffer(newCap));
                    meshletTrianglesAddress = rd.getBufferDeviceAddress(*meshletTrianglesBuffer);
                    grew                    = true;
                }
                if (grew)
                    rd.uploadS(*meshletTrianglesBuffer, 0, requiredBytes, cpuMeshletTriangles.data());
                else
                    rd.uploadS(*meshletTrianglesBuffer,
                               static_cast<uint64_t>(base) * sizeof(uint32_t),
                               static_cast<uint64_t>(count) * sizeof(uint32_t),
                               data);
                return base;
            }
        } meshlets;

        // Global material parameter pool (GPU buffer + CPU mirror).
        // Materials store offsets into this buffer.
        MaterialBuffer materialParams;

        // Bindless textures: stable slot indices.
        // Slot 0 is reserved for fallback.
        // NOTE: Do NOT erase/shrink this vector during runtime.
        std::vector<GpuTexture>  textures;
        std::vector<GpuMaterial> materials;
        std::vector<GpuMesh>     meshes;

        struct GaussianStorage
        {
            Ref<rhi::StorageBuffer> centersBuffer {nullptr};
            Ref<rhi::StorageBuffer> scaleBuffer {nullptr};
            Ref<rhi::StorageBuffer> covarianceBuffer {nullptr};
            Ref<rhi::StorageBuffer> colorBuffer {nullptr};
            Ref<rhi::StorageBuffer> shBuffer {nullptr};

            std::vector<glm::vec4>  cpuCenters;
            std::vector<glm::vec4>  cpuScales;
            std::vector<glm::uvec4> cpuCovariances;
            std::vector<glm::uvec2> cpuColors;
            std::vector<glm::uvec2> cpuSh;

            void reset()
            {
                centersBuffer    = nullptr;
                scaleBuffer      = nullptr;
                covarianceBuffer = nullptr;
                colorBuffer      = nullptr;
                shBuffer         = nullptr;
                cpuCenters.clear();
                cpuScales.clear();
                cpuCovariances.clear();
                cpuColors.clear();
                cpuSh.clear();
            }

            uint32_t appendCenters(rhi::RenderDevice& rd, const glm::vec4* data, uint32_t count)
            {
                if (!data || count == 0)
                    return static_cast<uint32_t>(cpuCenters.size());

                const uint32_t base = static_cast<uint32_t>(cpuCenters.size());
                cpuCenters.insert(cpuCenters.end(), data, data + count);
                const uint64_t requiredBytes = static_cast<uint64_t>(cpuCenters.size()) * sizeof(glm::vec4);
                bool           grew          = false;
                if (!centersBuffer || static_cast<uint64_t>(centersBuffer->getSize()) < requiredBytes)
                {
                    uint64_t oldCap = centersBuffer ? static_cast<uint64_t>(centersBuffer->getSize()) : 0ull;
                    uint64_t newCap = oldCap == 0 ? 256ull * 1024ull : oldCap * 2ull;
                    if (newCap < requiredBytes)
                        newCap = requiredBytes;
                    centersBuffer = createRef<rhi::StorageBuffer>(rd.createStorageBuffer(newCap));
                    grew          = true;
                }

                if (grew)
                    rd.uploadS(*centersBuffer, 0, requiredBytes, cpuCenters.data());
                else
                    rd.uploadS(*centersBuffer,
                               static_cast<uint64_t>(base) * sizeof(glm::vec4),
                               static_cast<uint64_t>(count) * sizeof(glm::vec4),
                               data);
                return base;
            }

            uint32_t appendScales(rhi::RenderDevice& rd, const glm::vec4* data, uint32_t count)
            {
                if (!data || count == 0)
                    return static_cast<uint32_t>(cpuScales.size());

                const uint32_t base = static_cast<uint32_t>(cpuScales.size());
                cpuScales.insert(cpuScales.end(), data, data + count);
                const uint64_t requiredBytes = static_cast<uint64_t>(cpuScales.size()) * sizeof(glm::vec4);
                bool           grew          = false;
                if (!scaleBuffer || static_cast<uint64_t>(scaleBuffer->getSize()) < requiredBytes)
                {
                    uint64_t oldCap = scaleBuffer ? static_cast<uint64_t>(scaleBuffer->getSize()) : 0ull;
                    uint64_t newCap = oldCap == 0 ? 256ull * 1024ull : oldCap * 2ull;
                    if (newCap < requiredBytes)
                        newCap = requiredBytes;
                    scaleBuffer = createRef<rhi::StorageBuffer>(rd.createStorageBuffer(newCap));
                    grew        = true;
                }

                if (grew)
                    rd.uploadS(*scaleBuffer, 0, requiredBytes, cpuScales.data());
                else
                    rd.uploadS(*scaleBuffer,
                               static_cast<uint64_t>(base) * sizeof(glm::vec4),
                               static_cast<uint64_t>(count) * sizeof(glm::vec4),
                               data);
                return base;
            }

            uint32_t appendCovariances(rhi::RenderDevice& rd, const glm::uvec4* data, uint32_t count)
            {
                if (!data || count == 0)
                    return static_cast<uint32_t>(cpuCovariances.size());

                const uint32_t base = static_cast<uint32_t>(cpuCovariances.size());
                cpuCovariances.insert(cpuCovariances.end(), data, data + count);
                const uint64_t requiredBytes = static_cast<uint64_t>(cpuCovariances.size()) * sizeof(glm::uvec4);
                bool           grew          = false;
                if (!covarianceBuffer || static_cast<uint64_t>(covarianceBuffer->getSize()) < requiredBytes)
                {
                    uint64_t oldCap = covarianceBuffer ? static_cast<uint64_t>(covarianceBuffer->getSize()) : 0ull;
                    uint64_t newCap = oldCap == 0 ? 256ull * 1024ull : oldCap * 2ull;
                    if (newCap < requiredBytes)
                        newCap = requiredBytes;
                    covarianceBuffer = createRef<rhi::StorageBuffer>(rd.createStorageBuffer(newCap));
                    grew             = true;
                }

                if (grew)
                    rd.uploadS(*covarianceBuffer, 0, requiredBytes, cpuCovariances.data());
                else
                    rd.uploadS(*covarianceBuffer,
                               static_cast<uint64_t>(base) * sizeof(glm::uvec4),
                               static_cast<uint64_t>(count) * sizeof(glm::uvec4),
                               data);
                return base;
            }

            uint32_t appendColors(rhi::RenderDevice& rd, const glm::uvec2* data, uint32_t count)
            {
                if (!data || count == 0)
                    return static_cast<uint32_t>(cpuColors.size());

                const uint32_t base = static_cast<uint32_t>(cpuColors.size());
                cpuColors.insert(cpuColors.end(), data, data + count);
                const uint64_t requiredBytes = static_cast<uint64_t>(cpuColors.size()) * sizeof(glm::uvec2);
                bool           grew          = false;
                if (!colorBuffer || static_cast<uint64_t>(colorBuffer->getSize()) < requiredBytes)
                {
                    uint64_t oldCap = colorBuffer ? static_cast<uint64_t>(colorBuffer->getSize()) : 0ull;
                    uint64_t newCap = oldCap == 0 ? 256ull * 1024ull : oldCap * 2ull;
                    if (newCap < requiredBytes)
                        newCap = requiredBytes;
                    colorBuffer = createRef<rhi::StorageBuffer>(rd.createStorageBuffer(newCap));
                    grew        = true;
                }

                if (grew)
                    rd.uploadS(*colorBuffer, 0, requiredBytes, cpuColors.data());
                else
                    rd.uploadS(*colorBuffer,
                               static_cast<uint64_t>(base) * sizeof(glm::uvec2),
                               static_cast<uint64_t>(count) * sizeof(glm::uvec2),
                               data);
                return base;
            }

            uint32_t appendSh(rhi::RenderDevice& rd, const glm::uvec2* data, uint32_t count)
            {
                if (!data || count == 0)
                    return static_cast<uint32_t>(cpuSh.size());

                const uint32_t base = static_cast<uint32_t>(cpuSh.size());
                cpuSh.insert(cpuSh.end(), data, data + count);
                const uint64_t requiredBytes = static_cast<uint64_t>(cpuSh.size()) * sizeof(glm::uvec2);
                bool           grew          = false;
                if (!shBuffer || static_cast<uint64_t>(shBuffer->getSize()) < requiredBytes)
                {
                    uint64_t oldCap = shBuffer ? static_cast<uint64_t>(shBuffer->getSize()) : 0ull;
                    uint64_t newCap = oldCap == 0 ? 512ull * 1024ull : oldCap * 2ull;
                    if (newCap < requiredBytes)
                        newCap = requiredBytes;
                    shBuffer = createRef<rhi::StorageBuffer>(rd.createStorageBuffer(newCap));
                    grew     = true;
                }

                if (grew)
                    rd.uploadS(*shBuffer, 0, requiredBytes, cpuSh.data());
                else
                    rd.uploadS(*shBuffer,
                               static_cast<uint64_t>(base) * sizeof(glm::uvec2),
                               static_cast<uint64_t>(count) * sizeof(glm::uvec2),
                               data);
                return base;
            }
        } gaussianStorage;

        std::vector<GpuGaussianSplat> gaussianSplats;
        Ref<rhi::StorageBuffer>       gaussianSplatMetaBuffer {nullptr};

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
                // WebGPU texture upload path is still being brought up.
                // Keep slot0 reserved to prevent crashes in bindless indexing.
                if (rd.getBackendApi() != rhi::RenderBackendApi::eWebGPU)
                {
                    fallback.texture = rd.createDefaultWhite1x1Texture2D();
                }
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
            meshlets.reset();
            gaussianStorage.reset();
            textures.clear();
            materials.clear();
            meshes.clear();
            gaussianSplats.clear();
            gaussianSplatMetaBuffer = nullptr;
            materialTableBuffer     = nullptr;
            materialParams.reset();
            freeTextureSlots.clear();
            pendingTextureFrees.clear();
        }

        void uploadGaussianSplatMeta(rhi::RenderDevice& rd)
        {
            const size_t bytes = gaussianSplats.size() * sizeof(GpuGaussianSplatMeta);
            if (bytes == 0)
            {
                gaussianSplatMetaBuffer = nullptr;
                return;
            }

            std::vector<GpuGaussianSplatMeta> meta;
            meta.reserve(gaussianSplats.size());
            for (const auto& splat : gaussianSplats)
            {
                meta.push_back(GpuGaussianSplatMeta {
                    .pointOffset      = splat.pointOffset,
                    .pointCount       = splat.pointCount,
                    .shDegree         = static_cast<uint32_t>(std::max(splat.shDegree, 0)),
                    .shRestCoeffCount = splat.shRestCoeffCount,
                });
            }

            if (!gaussianSplatMetaBuffer || gaussianSplatMetaBuffer->getSize() < bytes)
                gaussianSplatMetaBuffer = createRef<rhi::StorageBuffer>(rd.createStorageBuffer(bytes));

            rd.uploadS(*gaussianSplatMetaBuffer, 0, bytes, meta.data());
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

        std::vector<const rhi::Texture*> getBindlessTextureHandles() const
        {
            std::vector<const rhi::Texture*> out;
            out.reserve(textures.size());
            // insert by the bindless index order, which is the same as the vector order.
            for (const auto& tex : textures)
            {
                uint32_t idx = tex.bindlessIndex;
                if (idx >= out.size())
                    out.resize(idx + 1, nullptr);
                out[idx] = tex.texture.get();
            }
            return out;
        }
    };
} // namespace vultra::resource
