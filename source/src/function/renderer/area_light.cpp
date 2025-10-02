#include "vultra/function/renderer/area_light.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/function/renderer/default_vertex.hpp"

#include <glm/vec3.hpp>

namespace vultra
{
    namespace gfx
    {
        Ref<DefaultMesh> createAreaLightMesh(rhi::RenderDevice& rd,
                                             const glm::vec3&   position,
                                             float              width,
                                             float              height,
                                             const glm::vec4&   emissiveColorIntensity,
                                             bool               twoSided)
        {
            auto outMesh = createRef<DefaultMesh>();

            outMesh->subMeshes.clear();
            outMesh->materials.clear();
            outMesh->lights.clear();
            outMesh->vertices.clear();
            outMesh->indices.clear();

            outMesh->vertexFormat = SimpleVertex::getVertexFormat();

            outMesh->materials.push_back({
                .name                   = "AreaLightMaterial",
                .emissiveColorIntensity = emissiveColorIntensity,
                .doubleSided            = twoSided,
            });

            // Create a simple quad mesh for the area light
            outMesh->vertices.resize(4);
            auto& vertices       = outMesh->vertices;
            vertices[0].position = position + glm::vec3(-width / 2.0f, 0.0f, -height / 2.0f);
            vertices[1].position = position + glm::vec3(width / 2.0f, 0.0f, -height / 2.0f);
            vertices[2].position = position + glm::vec3(width / 2.0f, 0.0f, height / 2.0f);
            vertices[3].position = position + glm::vec3(-width / 2.0f, 0.0f, height / 2.0f);

            outMesh->indices.resize(6);
            auto& indices = outMesh->indices;
            indices       = {0, 1, 2, 2, 3, 0};

            SubMesh subMesh {};
            subMesh.name          = "AreaLightQuad";
            subMesh.vertexOffset  = 0;
            subMesh.indexOffset   = 0;
            subMesh.vertexCount   = 4;
            subMesh.indexCount    = 6;
            subMesh.materialIndex = 0;
            outMesh->subMeshes.push_back(subMesh);

            outMesh->vertexBuffer =
                createRef<rhi::VertexBuffer>(rd.createVertexBuffer(outMesh->getVertexStride(), vertices.size()));
            outMesh->indexBuffer =
                createRef<rhi::IndexBuffer>(rd.createIndexBuffer(rhi::IndexType::eUInt32, indices.size()));

            auto stagingVertexBuffer = rd.createStagingBuffer(sizeof(SimpleVertex) * vertices.size(), vertices.data());
            auto stagingIndexBuffer  = rd.createStagingBuffer(sizeof(uint32_t) * indices.size(), indices.data());

            rd.execute(
                [&](rhi::CommandBuffer& cb) {
                    cb.copyBuffer(stagingVertexBuffer,
                                  *outMesh->vertexBuffer,
                                  vk::BufferCopy {0, 0, stagingVertexBuffer.getSize()});
                    cb.copyBuffer(
                        stagingIndexBuffer, *outMesh->indexBuffer, vk::BufferCopy {0, 0, stagingIndexBuffer.getSize()});
                },
                true);

            outMesh->aabb = AABB::build(vertices);

            // Build the render mesh for ray tracing or ray query if supported
            if (HasFlagValues(rd.getFeatureFlag(), rhi::RenderDeviceFeatureFlagBits::eRayTracingPipeline) ||
                HasFlagValues(rd.getFeatureFlag(), rhi::RenderDeviceFeatureFlagBits::eRayQuery))
            {
                outMesh->buildRenderMesh(rd);
            }

            outMesh->buildMaterialBuffer(rd);

            return outMesh;
        }
    } // namespace gfx
} // namespace vultra