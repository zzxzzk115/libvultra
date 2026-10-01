#include "import_jobs.hpp"
#include "upload.hpp"

#include <vultra/core/base/logger.hpp>
#include <vultra/servers/rendering/meshlets.hpp>
#include <vultra/servers/rendering/scene.hpp>

#include <meshoptimizer.h>

#include <algorithm>
#include <limits>

namespace vultra
{
    static_assert(sizeof(Meshlet) == 32);

    MeshletData buildMeshlets(const SceneData& scene, uint32_t workers)
    {
        if (scene.vertices.empty() || scene.primitives.empty())
        {
            throw std::invalid_argument("Meshlets require nonempty scene geometry");
        }
        for (uint32_t index : scene.indices)
        {
            if (index >= scene.vertices.size())
            {
                throw std::invalid_argument("Meshlet vertex index exceeds the scene");
            }
        }
        size_t largestIndexCount = 0;
        for (const auto& primitive : scene.primitives)
        {
            if (primitive.indexCount % 3 != 0 ||
                uint64_t(primitive.firstIndex) + primitive.indexCount > scene.indices.size())
            {
                throw std::invalid_argument("Meshlets require valid triangle primitive ranges");
            }
            largestIndexCount = std::max(largestIndexCount, size_t(primitive.indexCount));
        }

        struct Job
        {
            std::vector<meshopt_Meshlet> meshlets;
            std::vector<uint32_t>        vertices;
            std::vector<unsigned char>   triangles;
        };

        std::vector<Job> jobs(scene.primitives.size());
        const auto*      positions = &scene.vertices.front().position.x;
        asset_detail::runImportJobs(
            "Building meshlets",
            uint32_t(jobs.size()),
            workers,
            scene.vertices.size() * 32 + largestIndexCount * 32,
            [&](uint32_t jobIndex)
            {
                const auto& primitive = scene.primitives[jobIndex];
                if (primitive.indexCount == 0)
                {
                    return;
                }
                auto& job = jobs[jobIndex];
                // Keep dev's per-submesh clusterization. SceneData indices are global here,
                // so rebase this primitive before passing it to meshoptimizer.
                const auto sourceIndices = std::span(scene.indices).subspan(primitive.firstIndex, primitive.indexCount);
                const auto [first, last] = std::minmax_element(sourceIndices.begin(), sourceIndices.end());
                const uint32_t         baseVertex = *first;
                std::vector<glm::vec3> positions(size_t(*last) - baseVertex + 1);
                for (size_t i = 0; i < positions.size(); ++i)
                {
                    positions[i] = scene.vertices[baseVertex + i].position;
                }
                std::vector<uint32_t> indices(sourceIndices.begin(), sourceIndices.end());
                for (auto& index : indices)
                {
                    index -= baseVertex;
                }
                const auto bound =
                    meshopt_buildMeshletsBound(primitive.indexCount, kMeshletVertices, kMeshletTriangles);
                job.meshlets.resize(bound);
                job.vertices.resize(bound * kMeshletVertices);
                job.triangles.resize(bound * kMeshletTriangles * 3);
                const auto count = meshopt_buildMeshlets(job.meshlets.data(),
                                                         job.vertices.data(),
                                                         job.triangles.data(),
                                                         indices.data(),
                                                         indices.size(),
                                                         &positions.front().x,
                                                         positions.size(),
                                                         sizeof(glm::vec3),
                                                         kMeshletVertices,
                                                         kMeshletTriangles,
                                                         0.5f);
                job.meshlets.resize(count);
                if (!job.meshlets.empty())
                {
                    const auto& lastMeshlet = job.meshlets.back();
                    job.vertices.resize(lastMeshlet.vertex_offset + lastMeshlet.vertex_count);
                    job.triangles.resize(lastMeshlet.triangle_offset + ((lastMeshlet.triangle_count * 3 + 3) & ~3u));
                }
                for (auto& vertex : job.vertices)
                {
                    vertex += baseVertex;
                }
            });
        MeshletData result;
        for (const auto& job : jobs)
        {
            result.primitives.push_back({uint32_t(result.meshlets.size()), uint32_t(job.meshlets.size())});
            for (const auto& meshlet : job.meshlets)
            {
                if (result.vertices.size() + meshlet.vertex_count > std::numeric_limits<uint32_t>::max() ||
                    result.triangles.size() + meshlet.triangle_count > std::numeric_limits<uint32_t>::max())
                {
                    throw std::runtime_error("Meshlet data exceeds 32-bit indexing");
                }
                const auto* vertices  = job.vertices.data() + meshlet.vertex_offset;
                const auto* triangles = job.triangles.data() + meshlet.triangle_offset;
                const auto  bounds    = meshopt_computeMeshletBounds(vertices,
                                                                     triangles,
                                                                     meshlet.triangle_count,
                                                                     positions,
                                                                     scene.vertices.size(),
                                                                     sizeof(SceneVertex));
                result.meshlets.push_back({{uint32_t(result.vertices.size()),
                                            uint32_t(result.triangles.size()),
                                            meshlet.vertex_count,
                                            meshlet.triangle_count},
                                           {bounds.center[0], bounds.center[1], bounds.center[2], bounds.radius}});
                result.vertices.insert(result.vertices.end(), vertices, vertices + meshlet.vertex_count);
                for (uint32_t triangle = 0; triangle < meshlet.triangle_count; ++triangle)
                {
                    const auto* corners = triangles + triangle * 3;
                    result.triangles.push_back(uint32_t(corners[0]) | (uint32_t(corners[1]) << 8) |
                                               (uint32_t(corners[2]) << 16));
                }
            }
        }
        Logger::core().info("Built {} meshlets, {} triangles, {} primitive ranges ({} vertices / {} triangles max)",
                            result.meshlets.size(),
                            result.triangles.size(),
                            result.primitives.size(),
                            kMeshletVertices,
                            kMeshletTriangles);
        return result;
    }

    GpuMeshlets::GpuMeshlets(Device& device, Buffer& vertices, const SceneData& scene, uint32_t workers) :
        m_Device(device)
    {
        auto data  = buildMeshlets(scene, workers);
        count      = uint32_t(data.meshlets.size());
        primitives = std::move(data.primitives);
        const VriAccessStage ready {VriAccess_ShaderResourceRead,
                                    VriPipelineStage_TaskShader | VriPipelineStage_MeshShader};
        const std::array<std::span<const std::byte>, 3> bytes {std::as_bytes(std::span(data.meshlets)),
                                                               std::as_bytes(std::span(data.vertices)),
                                                               std::as_bytes(std::span(data.triangles))};
        try
        {
            for (size_t i = 0; i < m_Buffers.size(); ++i)
            {
                m_Buffers[i] = uploadBuffer(device, bytes[i], VriBufferUsage_StorageBuffer, ready);
            }
            for (size_t i = 0; i < views.size(); ++i)
            {
                const auto&             buffer = i == 0 ? vertices : *m_Buffers[i - 1];
                const VriBufferViewDesc desc {buffer.handle,
                                              VriDescriptorType_StructuredBuffer,
                                              VriFormat_Unknown,
                                              0,
                                              buffer.desc.size};
                check(device.core.CreateBufferView(device.handle, &desc, &views[i]), "Create meshlet buffer view");
            }
        }
        catch (...)
        {
            for (auto* view : views)
            {
                if (view)
                {
                    device.core.DestroyDescriptor(view);
                }
            }
            throw;
        }
    }

    GpuMeshlets::~GpuMeshlets()
    {
        m_Device.waitIdle();
        for (auto* view : views)
        {
            m_Device.core.DestroyDescriptor(view);
        }
    }
} // namespace vultra
