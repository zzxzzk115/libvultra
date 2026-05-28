#include "vultra/function/resource/geometry_factory.hpp"

#include "vultra/function/resource/gpu_material.hpp"
#include "vultra/function/resource/gpu_mesh.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace vultra
{
    namespace
    {
        struct GeometryVertex
        {
            glm::vec3 position;
            glm::vec3 normal;
            glm::vec2 texCoord0;
        };

        struct alignas(16) MaterialParamsUnlit
        {
            glm::vec4 color {1.0f};
            uint32_t  colorTex {0};
            uint32_t  pad0 {0};
            uint32_t  pad1 {0};
            uint32_t  pad2 {0};
        };

        [[nodiscard]] rhi::VertexAttributes geometryVertexAttributes()
        {
            rhi::VertexAttributes attrs;
            attrs[0] =
                rhi::VertexAttribute {0, rhi::VertexAttribute::Type::eFloat3, offsetof(GeometryVertex, position)};
            attrs[1] = rhi::VertexAttribute {1, rhi::VertexAttribute::Type::eFloat3, offsetof(GeometryVertex, normal)};
            attrs[3] =
                rhi::VertexAttribute {3, rhi::VertexAttribute::Type::eFloat2, offsetof(GeometryVertex, texCoord0)};
            return attrs;
        }

        [[nodiscard]] uint32_t createMesh(IGpuResourceService&               gpuResources,
                                          rhi::RenderDevice&                 rd,
                                          const std::vector<GeometryVertex>& vertices,
                                          const std::vector<uint32_t>&       indices,
                                          const uint32_t                     materialIndex)
        {
            if (vertices.empty() || indices.empty())
                return std::numeric_limits<uint32_t>::max();

            GpuMeshCreateDesc desc;
            desc.vertexData        = vertices.data();
            desc.vertexDataBytes   = sizeof(GeometryVertex) * vertices.size();
            desc.vertexCount       = static_cast<uint32_t>(vertices.size());
            desc.vertexAttributes  = geometryVertexAttributes();
            desc.vertexStrideBytes = sizeof(GeometryVertex);
            desc.indexData         = indices.data();
            desc.indexCount        = static_cast<uint32_t>(indices.size());
            desc.indexType         = rhi::IndexType::eUInt32;
            desc.usage             = GpuMeshUsageFlags::eAll;

            const uint32_t meshIndex = gpuResources.createMesh(rd, desc);
            if (meshIndex == std::numeric_limits<uint32_t>::max())
                return meshIndex;

            auto& mesh          = gpuResources.pool().meshes[meshIndex];
            mesh.materialOffset = materialIndex;
            mesh.materialCount  = 1u;
            mesh.subMeshes.push_back(resource::GpuSubMesh {
                .vertexOffset  = 0u,
                .vertexCount   = static_cast<uint32_t>(vertices.size()),
                .indexOffset   = 0u,
                .indexCount    = static_cast<uint32_t>(indices.size()),
                .materialIndex = materialIndex,
            });
            return meshIndex;
        }

        [[nodiscard]] uint32_t
        createQuadMesh(IGpuResourceService& gpuResources, rhi::RenderDevice& rd, const uint32_t materialIndex)
        {
            const std::vector<GeometryVertex> vertices {
                {{-0.5f, 0.0f, -0.5f}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f}},
                {{0.5f, 0.0f, -0.5f}, {0.0f, -1.0f, 0.0f}, {1.0f, 0.0f}},
                {{0.5f, 0.0f, 0.5f}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f}},
                {{-0.5f, 0.0f, 0.5f}, {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f}},
            };
            const std::vector<uint32_t> indices {0, 1, 2, 0, 2, 3};
            return createMesh(gpuResources, rd, vertices, indices, materialIndex);
        }

        [[nodiscard]] uint32_t
        createCubeMesh(IGpuResourceService& gpuResources, rhi::RenderDevice& rd, const uint32_t materialIndex)
        {
            const std::array<glm::vec3, 6>                normals {{
                {1.0f, 0.0f, 0.0f},
                {-1.0f, 0.0f, 0.0f},
                {0.0f, 1.0f, 0.0f},
                {0.0f, -1.0f, 0.0f},
                {0.0f, 0.0f, 1.0f},
                {0.0f, 0.0f, -1.0f},
            }};
            const std::array<std::array<glm::vec3, 4>, 6> faces {{
                {{{0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, -0.5f}}},
                {{{-0.5f, -0.5f, 0.5f}, {-0.5f, -0.5f, -0.5f}, {-0.5f, 0.5f, -0.5f}, {-0.5f, 0.5f, 0.5f}}},
                {{{-0.5f, 0.5f, -0.5f}, {0.5f, 0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}}},
                {{{-0.5f, -0.5f, 0.5f}, {0.5f, -0.5f, 0.5f}, {0.5f, -0.5f, -0.5f}, {-0.5f, -0.5f, -0.5f}}},
                {{{0.5f, -0.5f, 0.5f}, {-0.5f, -0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}}},
                {{{-0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, -0.5f}, {-0.5f, 0.5f, -0.5f}}},
            }};

            std::vector<GeometryVertex> vertices;
            std::vector<uint32_t>       indices;
            vertices.reserve(24);
            indices.reserve(36);
            for (size_t face = 0; face < faces.size(); ++face)
            {
                const uint32_t base = static_cast<uint32_t>(vertices.size());
                vertices.push_back({faces[face][0], normals[face], {0.0f, 0.0f}});
                vertices.push_back({faces[face][1], normals[face], {1.0f, 0.0f}});
                vertices.push_back({faces[face][2], normals[face], {1.0f, 1.0f}});
                vertices.push_back({faces[face][3], normals[face], {0.0f, 1.0f}});
                indices.insert(indices.end(), {base, base + 1u, base + 2u, base, base + 2u, base + 3u});
            }
            return createMesh(gpuResources, rd, vertices, indices, materialIndex);
        }

        [[nodiscard]] uint32_t
        createSphereMesh(IGpuResourceService& gpuResources, rhi::RenderDevice& rd, const uint32_t materialIndex)
        {
            constexpr uint32_t kSegments = 32u;
            constexpr uint32_t kRings    = 16u;
            constexpr float    kPi       = 3.14159265358979323846f;

            std::vector<GeometryVertex> vertices;
            std::vector<uint32_t>       indices;
            vertices.reserve((kSegments + 1u) * (kRings + 1u));

            for (uint32_t y = 0; y <= kRings; ++y)
            {
                const float v     = static_cast<float>(y) / static_cast<float>(kRings);
                const float theta = v * kPi;
                for (uint32_t x = 0; x <= kSegments; ++x)
                {
                    const float u   = static_cast<float>(x) / static_cast<float>(kSegments);
                    const float phi = u * kPi * 2.0f;
                    glm::vec3   normal {
                        std::sin(theta) * std::cos(phi),
                        std::cos(theta),
                        std::sin(theta) * std::sin(phi),
                    };
                    vertices.push_back({normal * 0.5f, normal, {u, v}});
                }
            }

            for (uint32_t y = 0; y < kRings; ++y)
            {
                for (uint32_t x = 0; x < kSegments; ++x)
                {
                    const uint32_t row0 = y * (kSegments + 1u);
                    const uint32_t row1 = (y + 1u) * (kSegments + 1u);
                    indices.insert(indices.end(),
                                   {row0 + x, row1 + x, row1 + x + 1u, row0 + x, row1 + x + 1u, row0 + x + 1u});
                }
            }
            return createMesh(gpuResources, rd, vertices, indices, materialIndex);
        }

        [[nodiscard]] uint32_t
        createCapsuleMesh(IGpuResourceService& gpuResources, rhi::RenderDevice& rd, const uint32_t materialIndex)
        {
            constexpr uint32_t kSegments        = 32u;
            constexpr uint32_t kHemisphereRings = 8u;
            constexpr float    kPi              = 3.14159265358979323846f;
            constexpr float    kRadius          = 0.5f;
            constexpr float    kHalfCylinder    = 0.5f;

            std::vector<GeometryVertex> vertices;
            std::vector<uint32_t>       indices;

            auto appendRing = [&](float y, float radius, float normalY, float v) {
                const uint32_t base = static_cast<uint32_t>(vertices.size());
                for (uint32_t x = 0; x <= kSegments; ++x)
                {
                    const float u   = static_cast<float>(x) / static_cast<float>(kSegments);
                    const float phi = u * kPi * 2.0f;
                    glm::vec3   radial {std::cos(phi), 0.0f, std::sin(phi)};
                    glm::vec3   normal = glm::normalize(glm::vec3 {radial.x * radius, normalY, radial.z * radius});
                    vertices.push_back({{radial.x * radius, y, radial.z * radius}, normal, {u, v}});
                }
                return base;
            };

            std::vector<uint32_t> rings;
            for (uint32_t ring = 0; ring <= kHemisphereRings; ++ring)
            {
                const float t     = static_cast<float>(ring) / static_cast<float>(kHemisphereRings);
                const float angle = t * kPi * 0.5f;
                const float y     = kHalfCylinder + std::cos(angle) * kRadius;
                const float r     = std::sin(angle) * kRadius;
                rings.push_back(appendRing(y, r, std::cos(angle), t * 0.25f));
            }
            rings.push_back(appendRing(-kHalfCylinder, kRadius, 0.0f, 0.5f));
            for (uint32_t ring = 1; ring <= kHemisphereRings; ++ring)
            {
                const float t     = static_cast<float>(ring) / static_cast<float>(kHemisphereRings);
                const float angle = t * kPi * 0.5f;
                const float y     = -kHalfCylinder - std::sin(angle) * kRadius;
                const float r     = std::cos(angle) * kRadius;
                rings.push_back(appendRing(y, r, -std::sin(angle), 0.5f + t * 0.5f));
            }

            for (size_t ring = 0; ring + 1u < rings.size(); ++ring)
            {
                const uint32_t row0 = rings[ring];
                const uint32_t row1 = rings[ring + 1u];
                for (uint32_t x = 0; x < kSegments; ++x)
                {
                    indices.insert(indices.end(),
                                   {row0 + x, row1 + x, row1 + x + 1u, row0 + x, row1 + x + 1u, row0 + x + 1u});
                }
            }
            return createMesh(gpuResources, rd, vertices, indices, materialIndex);
        }
    } // namespace

    uint32_t GeometryFactory::ensureUnlitMaterial(IGpuResourceService& gpuResources, rhi::RenderDevice& rd)
    {
        if (m_UnlitMaterialIndex != std::numeric_limits<uint32_t>::max())
            return m_UnlitMaterialIndex;

        auto& pool = gpuResources.pool();

        MaterialParamsUnlit params;
        params.color    = glm::vec4 {1.0f};
        params.colorTex = 0u;

        resource::GpuMaterial material;
        material.model            = resource::GpuMaterialModel::eUnlit;
        material.blockOffsetBytes = pool.materialParams.allocAndUpload(rd, &params, sizeof(params));
        material.tableIndex       = static_cast<uint32_t>(pool.materials.size());

        m_UnlitMaterialIndex = material.tableIndex;
        pool.materials.push_back(material);
        pool.materialTableDirty = true;
        gpuResources.markContentDirty();
        return m_UnlitMaterialIndex;
    }

    uint32_t GeometryFactory::getOrCreateMeshIndex(BuiltinGeometryKind  kind,
                                                   IGpuResourceService& gpuResources,
                                                   rhi::RenderDevice&   rd)
    {
        const auto index = static_cast<size_t>(kind);
        if (index >= m_MeshIndices.size())
            return std::numeric_limits<uint32_t>::max();

        if (m_MeshIndices[index] != std::numeric_limits<uint32_t>::max())
            return m_MeshIndices[index];

        const uint32_t materialIndex = ensureUnlitMaterial(gpuResources, rd);
        if (materialIndex == std::numeric_limits<uint32_t>::max())
            return materialIndex;

        switch (kind)
        {
            case BuiltinGeometryKind::eQuad:
                m_MeshIndices[index] = createQuadMesh(gpuResources, rd, materialIndex);
                break;
            case BuiltinGeometryKind::eCube:
                m_MeshIndices[index] = createCubeMesh(gpuResources, rd, materialIndex);
                break;
            case BuiltinGeometryKind::eSphere:
                m_MeshIndices[index] = createSphereMesh(gpuResources, rd, materialIndex);
                break;
            case BuiltinGeometryKind::eCapsule:
                m_MeshIndices[index] = createCapsuleMesh(gpuResources, rd, materialIndex);
                break;
        }

        return m_MeshIndices[index];
    }
} // namespace vultra
