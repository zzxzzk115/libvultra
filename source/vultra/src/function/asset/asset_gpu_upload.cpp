// AssetSystem GPU-upload translation unit.
//
// Split out of asset_system.cpp (see ai/workspace/asset-system-split-plan.md): the three
// member functions that turn a decoded CPU asset into a GPU resource via the GPU resource
// service. They are tightly coupled to m_GpuResourceService / m_RenderDevice and share no
// state with the rest of AssetSystem beyond those members, so they move cleanly together
// with the few anonymous-namespace helpers used only here.

#include "vultra/function/asset/asset_system.hpp"

#include "vultra/core/base/base.hpp" // createRef
#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/structs/render_mesh.hpp"
#include "vultra/core/rhi/structs/vertex_attributes.hpp"
#include "vultra/function/asset/mesh_vertex_packing.hpp"
#include "vultra/function/resource/vtexture_loader.hpp"
#include "vultra/function/services/gpu_resource_service.hpp"

#include <vasset/vgaussiansplat.hpp>
#include <vasset/vmaterial.hpp>

#include <glm/gtc/packing.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace vultra
{
    namespace
    {
        using namespace resource;
        using namespace rhi;
        using namespace vasset;

        [[nodiscard]] bool materialNeedsAnyHit(const vasset::VMaterial& material)
        {
            if (material.model != vasset::VMaterialModel::ePBRMetallicRoughness)
                return false;

            return material.core.pbrMR.alphaMode == vasset::VMaterialAlphaMode::eMask;
        }

        float sigmoid(float x) { return 1.0f / (1.0f + std::exp(-x)); }

        float clampToF16(float x)
        {
            // IEEE half max finite value.
            return std::clamp(x, -65504.0f, 65504.0f);
        }

        uint32_t packF16x2(float a, float b) { return glm::packHalf2x16(glm::vec2(clampToF16(a), clampToF16(b))); }

        uint32_t packF16x2Clamp01(float a, float b)
        {
            return glm::packHalf2x16(glm::vec2(std::clamp(a, 0.0f, 1.0f), std::clamp(b, 0.0f, 1.0f)));
        }

        // External asset/user-facing quaternion vectors stay in xyzw order.
        // GLM's quat constructor expects wxyz, so this is the only place where
        // we intentionally bridge between the two conventions for 3DGS assets.
        glm::quat sanitizeAndNormalizeQuatFromExternalXyzw(const glm::vec4& xyzw)
        {
            if (!std::isfinite(xyzw.x) || !std::isfinite(xyzw.y) || !std::isfinite(xyzw.z) || !std::isfinite(xyzw.w))
                return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

            glm::quat   q(xyzw.w, xyzw.x, xyzw.y, xyzw.z);
            const float len2 = glm::dot(q, q);
            if (!(len2 > 1e-12f))
                return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

            return glm::normalize(q);
        }
    } // namespace

    uint32_t AssetSystem::uploadTexture(const vasset::VTexture& cpuTex)
    {
        auto tr = resource::loadTextureFromVTexture(cpuTex, *m_RenderDevice);
        if (!tr)
        {
            std::string uri;
            m_Resolver.resolve(cpuTex.uuid, uri);
            VULTRA_CORE_ERROR("[AssetSystem] Failed to load texture from VTexture {}", uri);
            return 0;
        }

        resource::GpuTexture out;
        out.texture = createRef<rhi::Texture>(std::move(tr.value()));
        {
            std::string uri;
            m_Resolver.resolve(cpuTex.uuid, uri);
            const auto label = uri.empty() ? std::string("Asset Texture ") + vbase::to_string(cpuTex.uuid) :
                                             std::string("Asset Texture ") + uri;
            m_RenderDevice->updateMemoryResource(out.texture->getMemoryResourceId(), label, cpuTex.toString());
        }
        return m_GpuResourceService->createTexture(*m_RenderDevice, std::move(out));
    }

    uint32_t AssetSystem::uploadMesh(const vasset::VMesh& cpuMesh, uint32_t materialOffset)
    {
        if (!m_RenderDevice)
            return std::numeric_limits<uint32_t>::max();

        auto& pool = m_GpuResourceService->pool();

        uint32_t strideBytes = 0;
        auto     attrs       = buildVertexAttributes(cpuMesh.vertexFlags, strideBytes);

        // Pack CPU mesh into an AoS byte stream based on vertex flags.
        auto vertexData = packVertices(cpuMesh, {.stride = strideBytes, .attributes = attrs});

        GpuMeshCreateDesc desc;
        desc.vertexData        = vertexData.data();
        desc.vertexDataBytes   = static_cast<uint64_t>(vertexData.size());
        desc.vertexCount       = cpuMesh.vertexCount;
        desc.vertexAttributes  = attrs;
        desc.vertexStrideBytes = strideBytes;

        if (!cpuMesh.indices.empty())
        {
            desc.indexData  = cpuMesh.indices.data();
            desc.indexCount = static_cast<uint32_t>(cpuMesh.indices.size());
            desc.indexType  = rhi::IndexType::eUInt32;
        }

        std::vector<resource::GpuMeshlet> gpuMeshlets;
        std::vector<uint32_t>             gpuMeshletVertices;
        std::vector<uint32_t>             gpuMeshletTriangles;

        for (const auto& subMesh : cpuMesh.subMeshes)
        {
            const uint32_t subVertexBase     = subMesh.vertexOffset;
            const uint32_t subMaterialIndex  = materialOffset + subMesh.materialIndex;
            const uint32_t meshletVertexBase = static_cast<uint32_t>(gpuMeshletVertices.size());
            const uint32_t meshletTriBase    = static_cast<uint32_t>(gpuMeshletTriangles.size());

            for (uint32_t v : subMesh.meshletGroup.meshletVertices)
                gpuMeshletVertices.push_back(subVertexBase + v);
            for (uint8_t tri : subMesh.meshletGroup.meshletTriangles)
                gpuMeshletTriangles.push_back(static_cast<uint32_t>(tri));

            for (const auto& ml : subMesh.meshletGroup.meshlets)
            {
                resource::GpuMeshlet gm {};
                gm.vertexOffset   = meshletVertexBase + ml.vertexOffset;
                gm.vertexCount    = ml.vertexCount;
                gm.triangleOffset = meshletTriBase + ml.triangleOffset;
                gm.triangleCount  = ml.triangleCount;
                gm.materialIndex  = materialOffset + ml.materialIndex;
                gm.center         = ml.center;
                gm.radius         = ml.radius;
                gm.coneAxis       = ml.coneAxis;
                gm.coneCutoff     = ml.coneCutoff;
                gm.coneApex       = ml.coneApex;
                if (subMesh.meshletGroup.meshlets.empty())
                    gm.materialIndex = subMaterialIndex;
                gpuMeshlets.push_back(gm);
            }
        }

        desc.meshletData          = gpuMeshlets.data();
        desc.meshletCount         = static_cast<uint32_t>(gpuMeshlets.size());
        desc.meshletVertexData    = gpuMeshletVertices.data();
        desc.meshletVertexCount   = static_cast<uint32_t>(gpuMeshletVertices.size());
        desc.meshletTriangleData  = gpuMeshletTriangles.data();
        desc.meshletTriangleCount = static_cast<uint32_t>(gpuMeshletTriangles.size());

        // Asset meshes are generally useful in both CPU-driven and GPU-driven passes.
        desc.usage = GpuMeshUsageFlags::eAll;

        const uint32_t meshIndex = m_GpuResourceService->createMesh(*m_RenderDevice, desc);
        if (meshIndex == std::numeric_limits<uint32_t>::max())
            return meshIndex;

        pool.meshes[meshIndex].materialOffset = materialOffset;
        pool.meshes[meshIndex].materialCount  = static_cast<uint32_t>(cpuMesh.materials.size());
        pool.meshes[meshIndex].subMeshes.clear();
        pool.meshes[meshIndex].subMeshes.reserve(std::max<size_t>(cpuMesh.subMeshes.size(), 1u));
        for (const auto& subMesh : cpuMesh.subMeshes)
        {
            resource::GpuSubMesh gpuSubMesh {};
            gpuSubMesh.vertexOffset  = subMesh.vertexOffset;
            gpuSubMesh.vertexCount   = subMesh.vertexCount;
            gpuSubMesh.indexOffset   = subMesh.indexOffset;
            gpuSubMesh.indexCount    = subMesh.indexCount;
            gpuSubMesh.materialIndex = materialOffset + subMesh.materialIndex;
            pool.meshes[meshIndex].subMeshes.push_back(gpuSubMesh);
        }
        if (pool.meshes[meshIndex].subMeshes.empty() && cpuMesh.vertexCount > 0 && !cpuMesh.indices.empty())
        {
            pool.meshes[meshIndex].subMeshes.push_back(resource::GpuSubMesh {
                .vertexOffset  = 0u,
                .vertexCount   = cpuMesh.vertexCount,
                .indexOffset   = 0u,
                .indexCount    = static_cast<uint32_t>(cpuMesh.indices.size()),
                .materialIndex = materialOffset,
            });
        }
        pool.meshes[meshIndex].hasSkin          = cpuMesh.hasSkin;
        pool.meshes[meshIndex].skeleton         = CoreUUID(cpuMesh.skeleton);
        pool.meshes[meshIndex].inverseBindPoses = cpuMesh.inverseBindPoses;

        const bool rayTracingEnabled =
            HasFlagValues(m_RenderDevice->getFeatureFlag(), rhi::RenderDeviceFeatureFlagBits::eRayTracingPipeline);
        if (rayTracingEnabled && pool.meshes[meshIndex].vertexBuffer && pool.meshes[meshIndex].indexBuffer &&
            !pool.meshes[meshIndex].subMeshes.empty())
        {
            auto& gpuMesh = pool.meshes[meshIndex];

            const auto vertexAddress    = m_RenderDevice->getBufferDeviceAddress(gpuMesh.vertexBuffer);
            const auto indexAddress     = m_RenderDevice->getBufferDeviceAddress(gpuMesh.indexBuffer);
            gpuMesh.vertexBufferAddress = vertexAddress;
            gpuMesh.indexBufferAddress  = indexAddress;
            const auto     positionIt   = gpuMesh.vertexAttributes.find(0);
            const uint32_t positionOffsetBytes =
                positionIt != gpuMesh.vertexAttributes.end() ? positionIt->second.offset : 0u;

            std::vector<rhi::RenderSubMesh> rtSubMeshes;
            rtSubMeshes.reserve(gpuMesh.subMeshes.size());
            for (const auto& sm : gpuMesh.subMeshes)
            {
                if (sm.indexCount == 0)
                    continue;

                rhi::RenderSubMesh rtSubMesh {};
                rtSubMesh.vertexBufferAddress = vertexAddress;
                rtSubMesh.indexBufferAddress =
                    rhi::DeviceAddress {indexAddress.value + static_cast<uint64_t>(sm.indexOffset) * sizeof(uint32_t)};
                rtSubMesh.vertexStride        = gpuMesh.vertexStrideBytes;
                rtSubMesh.vertexCount         = gpuMesh.vertexCount;
                rtSubMesh.vertexOffset        = sm.vertexOffset;
                rtSubMesh.positionOffsetBytes = positionOffsetBytes;
                rtSubMesh.indexCount          = sm.indexCount;
                rtSubMesh.indexType           = rhi::IndexType::eUInt32;
                rtSubMesh.materialIndex       = sm.materialIndex;
                const uint32_t localMaterialIndex =
                    sm.materialIndex >= materialOffset ? sm.materialIndex - materialOffset : sm.materialIndex;
                rtSubMesh.opaque = localMaterialIndex >= cpuMesh.materials.size() ||
                                   !materialNeedsAnyHit(cpuMesh.materials[localMaterialIndex]);
                rtSubMeshes.push_back(rtSubMesh);
            }

            if (!rtSubMeshes.empty())
                gpuMesh.blas = m_RenderDevice->createBuildRenderMeshBLAS(rtSubMeshes);
        }

        // Remap meshlet vertex indices from local mesh space to global packed-vertex space.
        auto&          gpuMesh = pool.meshes[meshIndex];
        const uint32_t globalBaseVertex =
            gpuMesh.vertexStrideBytes > 0 ? (gpuMesh.vertexByteOffset / gpuMesh.vertexStrideBytes) : 0u;
        for (uint32_t i = 0; i < gpuMesh.meshletCount; ++i)
        {
            auto& gm = pool.meshlets.cpuMeshlets[gpuMesh.meshletOffset + i];
            for (uint32_t j = 0; j < gm.vertexCount; ++j)
                pool.meshlets.cpuMeshletVertices[gm.vertexOffset + j] += globalBaseVertex;
        }
        if (pool.meshlets.meshletVerticesBuffer && !pool.meshlets.cpuMeshletVertices.empty())
            m_RenderDevice->uploadS(*pool.meshlets.meshletVerticesBuffer,
                                    0,
                                    pool.meshlets.cpuMeshletVertices.size() * sizeof(uint32_t),
                                    pool.meshlets.cpuMeshletVertices.data());

        m_GpuResourceService->markContentDirty();

        return meshIndex;
    }

    uint32_t AssetSystem::uploadGaussianSplat(const vasset::VGaussianSplat& cpuSplat)
    {
        if (!m_RenderDevice)
            return std::numeric_limits<uint32_t>::max();

        auto& pool = m_GpuResourceService->pool();

        constexpr float kShC0          = 0.28209479177f;
        constexpr int   kTargetRest    = static_cast<int>(resource::GpuGaussianSplat::s_PackedShRestCoeffs);
        constexpr float kAlphaMinKeep  = 0.001f;
        constexpr float kAlphaLogitMin = -20.0f;
        constexpr float kAlphaLogitMax = 20.0f;
        constexpr float kLogScaleMin   = -20.0f;
        constexpr float kLogScaleMax   = 4.0f;

        const int fileDegree     = std::clamp(cpuSplat.shDegree, 0, 3);
        const int fileRestCoeffs = fileDegree > 0 ? (((fileDegree + 1) * (fileDegree + 1)) - 1) : 0;

        auto decodeAlpha = [&](const vasset::VGaussianSplatPoint& p) -> float {
            if (!std::isfinite(p.opacity))
                return 0.0f;

            return sigmoid(std::clamp(p.opacity, kAlphaLogitMin, kAlphaLogitMax));
        };

        auto decodeScaleLin = [&](const vasset::VGaussianSplatPoint& p) -> glm::vec3 {
            if (!std::isfinite(p.scale.x) || !std::isfinite(p.scale.y) || !std::isfinite(p.scale.z))
                return glm::vec3(1e-6f);

            return glm::vec3(std::exp(std::clamp(p.scale.x, kLogScaleMin, kLogScaleMax)),
                             std::exp(std::clamp(p.scale.y, kLogScaleMin, kLogScaleMax)),
                             std::exp(std::clamp(p.scale.z, kLogScaleMin, kLogScaleMax)));
        };

        auto decodeBaseRgb = [&](const vasset::VGaussianSplatPoint& p) -> glm::vec3 {
            if (!std::isfinite(p.shDC.x) || !std::isfinite(p.shDC.y) || !std::isfinite(p.shDC.z))
                return glm::vec3(0.0f);

            return glm::clamp(kShC0 * p.shDC + glm::vec3(0.5f), glm::vec3(0.0f), glm::vec3(1.0f));
        };

        std::vector<glm::vec4>  packedCenters;
        std::vector<glm::vec4>  packedScales;
        std::vector<glm::uvec4> packedCovariances;
        std::vector<glm::uvec2> packedColors;
        std::vector<glm::uvec2> packedSh;

        packedCenters.reserve(cpuSplat.splats.size());
        packedScales.reserve(cpuSplat.splats.size());
        packedCovariances.reserve(cpuSplat.splats.size());
        packedColors.reserve(cpuSplat.splats.size());
        packedSh.reserve(cpuSplat.splats.size() * resource::GpuGaussianSplat::s_PackedShRestCoeffs);

        for (size_t i = 0; i < cpuSplat.splats.size(); ++i)
        {
            const auto& p = cpuSplat.splats[i];
            if (!std::isfinite(p.position.x) || !std::isfinite(p.position.y) || !std::isfinite(p.position.z))
                continue;

            const float alpha = decodeAlpha(p);
            if (alpha < kAlphaMinKeep)
                continue;

            packedCenters.push_back(glm::vec4(p.position, 1.0f));

            const glm::vec3 baseRgb = decodeBaseRgb(p);
            packedColors.emplace_back(packF16x2Clamp01(baseRgb.r, baseRgb.g), packF16x2Clamp01(baseRgb.b, alpha));

            const glm::vec3 scaleLin = decodeScaleLin(p);
            packedScales.push_back(glm::vec4(scaleLin, 0.0f));
            const glm::quat q = sanitizeAndNormalizeQuatFromExternalXyzw(p.rotation);
            const glm::mat3 R = glm::mat3_cast(q);

            glm::mat3 D(0.0f);
            D[0][0] = scaleLin.x * scaleLin.x;
            D[1][1] = scaleLin.y * scaleLin.y;
            D[2][2] = scaleLin.z * scaleLin.z;

            const glm::mat3 Sigma = R * D * glm::transpose(R);

            const float m11 = Sigma[0][0];
            const float m12 = Sigma[1][0];
            const float m13 = Sigma[2][0];
            const float m22 = Sigma[1][1];
            const float m23 = Sigma[2][1];
            const float m33 = Sigma[2][2];

            packedCovariances.emplace_back(packF16x2(m11, m12), packF16x2(m13, m22), packF16x2(m23, m33), 0u);

            const size_t pointBase = i * static_cast<size_t>(fileRestCoeffs) * 3ull;
            for (int k = 0; k < kTargetRest; ++k)
            {
                float rr = 0.0f;
                float gg = 0.0f;
                float bb = 0.0f;
                if (fileRestCoeffs > 0 && k < fileRestCoeffs)
                {
                    const size_t coeffBase = pointBase + static_cast<size_t>(k) * 3ull;
                    if (coeffBase + 2ull < cpuSplat.sh.size())
                    {
                        rr = cpuSplat.sh[coeffBase + 0ull];
                        gg = cpuSplat.sh[coeffBase + 1ull];
                        bb = cpuSplat.sh[coeffBase + 2ull];
                    }

                    if (!std::isfinite(rr))
                        rr = 0.0f;
                    if (!std::isfinite(gg))
                        gg = 0.0f;
                    if (!std::isfinite(bb))
                        bb = 0.0f;

                    rr = std::clamp(rr, -10.0f, 10.0f);
                    gg = std::clamp(gg, -10.0f, 10.0f);
                    bb = std::clamp(bb, -10.0f, 10.0f);
                }

                packedSh.emplace_back(packF16x2(rr, gg), packF16x2(bb, 0.0f));
            }
        }

        if (packedCenters.empty())
        {
            VULTRA_CORE_ERROR("[AssetSystem] uploadGaussianSplat: no valid points in '{}'.", cpuSplat.name);
            return std::numeric_limits<uint32_t>::max();
        }

        glm::vec3 center(0.0f);
        glm::vec3 minP(std::numeric_limits<float>::infinity());
        glm::vec3 maxP(-std::numeric_limits<float>::infinity());
        for (const auto& c : packedCenters)
        {
            minP = glm::min(minP, glm::vec3(c));
            maxP = glm::max(maxP, glm::vec3(c));
        }
        center = 0.5f * (minP + maxP);

        float radius = 0.0f;
        for (const auto& c : packedCenters)
            radius = std::max(radius, glm::length(glm::vec3(c) - center));

        resource::GpuGaussianSplat out;
        out.pointCount  = static_cast<uint32_t>(packedCenters.size());
        out.shDegree    = fileDegree;
        out.center      = center;
        out.radius      = radius;
        out.pointOffset = pool.gaussianStorage.appendCenters(*m_RenderDevice, packedCenters.data(), out.pointCount);
        pool.gaussianStorage.appendScales(*m_RenderDevice, packedScales.data(), out.pointCount);
        pool.gaussianStorage.appendCovariances(*m_RenderDevice, packedCovariances.data(), out.pointCount);
        pool.gaussianStorage.appendColors(*m_RenderDevice, packedColors.data(), out.pointCount);
        pool.gaussianStorage.appendSh(*m_RenderDevice, packedSh.data(), static_cast<uint32_t>(packedSh.size()));

        const uint32_t index = static_cast<uint32_t>(pool.gaussianSplats.size());
        pool.gaussianSplats.push_back(std::move(out));
        pool.uploadGaussianSplatMeta(*m_RenderDevice);
        m_GpuResourceService->markContentDirty();
        return index;
    }
} // namespace vultra
