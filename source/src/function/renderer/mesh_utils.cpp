#include "vultra/function/renderer/mesh_utils.hpp"
#include "vultra/function/renderer/shader_config/shader_config.hpp"

#include <meshoptimizer.h>

namespace vultra
{
    namespace gfx
    {
        void generateMeshlets(DefaultMesh& mesh)
        {
            // --- Build meshlets per submesh ---
            for (auto& sub : mesh.subMeshes)
            {
                // Collect positions for meshoptimizer
                std::vector<glm::vec3> positions;
                positions.reserve(sub.vertexCount);
                for (uint32_t v = 0; v < sub.vertexCount; ++v)
                {
                    const auto& vertex = mesh.vertices[sub.vertexOffset + v];
                    positions.push_back(vertex.position);
                }

                // Extra: meshlets
                // https://github.com/zeux/meshoptimizer/tree/v0.24#clusterization
                sub.meshletGroup.meshlets.clear();
                sub.meshletGroup.meshletTriangles.clear();
                sub.meshletGroup.meshletVertices.clear();

                const size_t maxVerts = MAX_MESHLET_VERTICES;
                const size_t maxTris  = MAX_MESHLET_TRIANGLES;

                const uint32_t* subIndices = mesh.indices.data() + sub.indexOffset;
                size_t          subCount   = sub.indexCount;

                // allocate space for meshlet vertices and triangles
                size_t                       maxMeshlets = meshopt_buildMeshletsBound(subCount, maxVerts, maxTris);
                std::vector<meshopt_Meshlet> meshletsTemp(maxMeshlets);

                // store current offsets
                size_t baseVertOffset = sub.meshletGroup.meshletVertices.size();
                size_t baseTriOffset  = sub.meshletGroup.meshletTriangles.size();

                sub.meshletGroup.meshletVertices.resize(baseVertOffset + maxMeshlets * maxVerts);
                sub.meshletGroup.meshletTriangles.resize(baseTriOffset + maxMeshlets * maxTris * 3);

                // build meshlets
                size_t meshletCount = meshopt_buildMeshlets(meshletsTemp.data(),
                                                            sub.meshletGroup.meshletVertices.data() + baseVertOffset,
                                                            sub.meshletGroup.meshletTriangles.data() + baseTriOffset,
                                                            subIndices,
                                                            subCount,
                                                            reinterpret_cast<const float*>(positions.data()),
                                                            positions.size(),
                                                            sizeof(glm::vec3),
                                                            maxVerts,
                                                            maxTris,
                                                            0.5f);

                meshletsTemp.resize(meshletCount);

                // copy to Meshlet
                for (size_t i = 0; i < meshletCount; ++i)
                {
                    const auto& src = meshletsTemp[i];
                    Meshlet     dst {};

                    dst.vertexOffset   = baseVertOffset + src.vertex_offset;
                    dst.triangleOffset = baseTriOffset + src.triangle_offset;
                    dst.vertexCount    = src.vertex_count;
                    dst.triangleCount  = src.triangle_count;

                    // --- Assign material index directly from submesh ---
                    dst.materialIndex = sub.materialIndex;

                    auto bounds = meshopt_computeMeshletBounds(&sub.meshletGroup.meshletVertices[dst.vertexOffset],
                                                               &sub.meshletGroup.meshletTriangles[dst.triangleOffset],
                                                               dst.triangleCount,
                                                               reinterpret_cast<const float*>(positions.data()),
                                                               positions.size(),
                                                               sizeof(glm::vec3));

                    dst.center     = glm::vec3(bounds.center[0], bounds.center[1], bounds.center[2]);
                    dst.radius     = bounds.radius;
                    dst.coneAxis   = glm::vec3(bounds.cone_axis[0], bounds.cone_axis[1], bounds.cone_axis[2]);
                    dst.coneCutoff = bounds.cone_cutoff;
                    dst.coneApex   = glm::vec3(bounds.cone_apex[0], bounds.cone_apex[1], bounds.cone_apex[2]);

                    sub.meshletGroup.meshlets.push_back(dst);
                }

                // Resize to actual used size
                const auto& last = sub.meshletGroup.meshlets.back();

                sub.meshletGroup.meshletVertices.resize(last.vertexOffset + last.vertexCount);
                sub.meshletGroup.meshletTriangles.resize(last.triangleOffset + ((last.triangleCount * 3 + 3) & ~3));
            }
        }
    } // namespace gfx
} // namespace vultra