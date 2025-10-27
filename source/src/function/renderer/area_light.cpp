#include "vultra/function/renderer/area_light.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/function/renderer/default_vertex.hpp"
#include "vultra/function/renderer/mesh_utils.hpp"
#include "vultra/function/scenegraph/components.hpp"

#include <glm/ext/matrix_transform.hpp>
#include <glm/vec3.hpp>

namespace vultra
{
    namespace gfx
    {
        Ref<DefaultMesh> createAreaLightMesh(rhi::RenderDevice&        rd,
                                             const AreaLightComponent& lightComponent,
                                             const TransformComponent& lightTransform)
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
                .emissiveColorIntensity = glm::vec4(lightComponent.color, lightComponent.intensity),
                .doubleSided            = lightComponent.twoSided,
            });

            // Create a simple quad mesh for the area light
            outMesh->vertices.resize(4);
            auto& vertices = outMesh->vertices;

            // Set vertices with rotation
            glm::mat4 transform = lightTransform.getTransform();
            float     width     = lightComponent.width;
            float     height    = lightComponent.height;

            glm::vec3 center      = lightTransform.position;
            glm::vec3 halfExtents = glm::vec3(width * 0.5f, height * 0.5f, 0.0f);

            vertices[0].position = glm::vec3(transform * glm::vec4(-halfExtents.x, -halfExtents.y, 0.0f, 1.0f));
            vertices[1].position = glm::vec3(transform * glm::vec4(halfExtents.x, -halfExtents.y, 0.0f, 1.0f));
            vertices[2].position = glm::vec3(transform * glm::vec4(halfExtents.x, halfExtents.y, 0.0f, 1.0f));
            vertices[3].position = glm::vec3(transform * glm::vec4(-halfExtents.x, halfExtents.y, 0.0f, 1.0f));

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

            // Generate meshlets
            generateMeshlets(*outMesh);

            outMesh->buildMaterialBuffer(rd);

            // Build meshlet buffers for each sub-mesh
            for (auto& sm : outMesh->subMeshes)
            {
                sm.buildMeshletBuffers(rd);
            }

            // Build the render mesh for ray tracing, ray query or mesh shading if supported
            if (HasFlagValues(rd.getFeatureFlag(), rhi::RenderDeviceFeatureFlagBits::eRayTracingPipeline) ||
                HasFlagValues(rd.getFeatureFlag(), rhi::RenderDeviceFeatureFlagBits::eRayQuery) ||
                HasFlagValues(rd.getFeatureFlag(), rhi::RenderDeviceFeatureFlagBits::eMeshShader))
            {
                outMesh->buildRenderMesh(rd);
            }

            return outMesh;
        }
    } // namespace gfx
} // namespace vultra