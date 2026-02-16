#pragma once

#include "vultra/core/base/base.hpp"
#include "vultra/core/math/aabb.hpp"
#include "vultra/core/rhi/index_buffer.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/core/rhi/render_mesh.hpp"
#include "vultra/core/rhi/storage_buffer.hpp"
#include "vultra/core/rhi/vertex_buffer.hpp"
#include "vultra/function/renderer/vertex_format.hpp"

#include <vasset/vmesh.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace vultra::gfx
{
    struct alignas(16) GPUMaterial
    {
        uint32_t albedoIndex;
        uint32_t alphaMaskIndex;
        uint32_t metallicIndex;
        uint32_t roughnessIndex;

        uint32_t specularIndex;
        uint32_t normalIndex;
        uint32_t aoIndex;
        uint32_t emissiveIndex;

        uint32_t metallicRoughnessIndex;
        uint32_t paddingUI0;
        uint32_t paddingUI1;
        uint32_t paddingUI2;

        glm::vec4 baseColor;
        glm::vec4 emissiveColorIntensity;
        glm::vec4 ambientColor;

        float opacity;
        float metallicFactor;
        float roughnessFactor;
        float ior;

        float alphaCutoff;
        float paddingF0;
        float paddingF1;
        float paddingF2;

        int alphaMode;
        int doubleSided;
        int paddingI0;
        int paddingI1;
    };
    static_assert(sizeof(GPUMaterial) % 16 == 0);

    struct GPUGeometryNode
    {
        uint64_t vertexBufferAddress {0};
        uint64_t indexBufferAddress {0};
        uint32_t materialIndex {0};
    };

    using GPUMeshlet = vasset::VMeshlet;
    static_assert(sizeof(GPUMeshlet) % 16 == 0);

    struct MeshletGroup
    {
        Ref<rhi::StorageBuffer> meshletBuffer;
        Ref<rhi::StorageBuffer> meshletVertexBuffer;
        Ref<rhi::StorageBuffer> meshletTriangleBuffer;

        uint32_t meshletCount {0};
    };

    struct SubMesh
    {
        std::string name;

        uint32_t vertexOffset {0};
        uint32_t vertexCount {0};

        uint32_t indexOffset {0};
        uint32_t indexCount {0};

        uint32_t materialIndex {0};

        AABB aabb;

        MeshletGroup meshlets;
    };

    class Mesh
    {
    public:
        // GPU buffers
        struct GPUBuffers
        {
            Ref<rhi::VertexBuffer>  vertexBuffer;
            Ref<rhi::IndexBuffer>   indexBuffer;
            Ref<rhi::StorageBuffer> materialBuffer;
        } gpuBuffers;

        struct Info
        {
            vbase::UUID uuid {};

            uint32_t vertexStride {0};
            uint32_t vertexCount {0};
            uint32_t indexCount {0};
            uint32_t materialCount {0};

            Ref<VertexFormat> vertexFormat {nullptr};
        } info;

        std::vector<SubMesh> subMeshes;
        rhi::RenderMesh      renderMesh {};

    public:
        static Ref<Mesh> create(rhi::RenderDevice& rd, const vasset::VMesh& asset);

        void rebuildRenderMesh(rhi::RenderDevice& rd); // for device feature changes / buffer rebind

    private:
        void buildFromAsset(rhi::RenderDevice& rd, const vasset::VMesh& asset);

        void buildVertexFormat(vasset::VVertexFlags flags);
        void buildVertexAndIndexBuffers(rhi::RenderDevice& rd, const vasset::VMesh& asset);
        void buildMaterialBuffer(rhi::RenderDevice& rd, const vasset::VMesh& asset);
        void buildSubMeshesAndMeshlets(rhi::RenderDevice& rd, const vasset::VMesh& asset);
        void buildRenderMeshInternal(rhi::RenderDevice& rd);

    private:
        // Helpers
        static AABB computeAABBForRange(const std::vector<vasset::VPosition>& positions,
                                        uint32_t                              vertexOffset,
                                        uint32_t                              vertexCount);

        static Ref<rhi::VertexBuffer> createVertexBufferRaw(rhi::RenderDevice& rd, size_t sizeBytes);
        static Ref<rhi::IndexBuffer>  createIndexBufferRaw(rhi::RenderDevice& rd, size_t sizeBytes);
    };

} // namespace vultra::gfx
