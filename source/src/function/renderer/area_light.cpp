#include "vultra/function/renderer/area_light.hpp"
#include "vultra/function/scenegraph/components.hpp"

#include <glm/ext/matrix_transform.hpp>
#include <glm/vec3.hpp>

namespace vultra
{
    namespace gfx
    {
        Ref<Mesh> createAreaLightMesh(rhi::RenderDevice&        rd,
                                      const AreaLightComponent& lightComponent,
                                      const TransformComponent& lightTransform)
        {
            // Build procedural quad asset
            vasset::VMesh asset {};

            asset.vertexFlags =
                vasset::VVertexFlags::ePosition | vasset::VVertexFlags::eNormal | vasset::VVertexFlags::eTexCoord0;

            constexpr uint32_t vertexCount = 4;
            constexpr uint32_t indexCount  = 6;

            asset.positions.resize(vertexCount);
            asset.normals.resize(vertexCount);
            asset.texCoords0.resize(vertexCount);
            asset.indices.resize(indexCount);

            // Quad corners in local space
            const float w = lightComponent.width;
            const float h = lightComponent.height;

            const glm::vec3 halfExtents(w * 0.5f, h * 0.5f, 0.0f);

            const glm::vec3 p0(-halfExtents.x, -halfExtents.y, 0.0f);
            const glm::vec3 p1(halfExtents.x, -halfExtents.y, 0.0f);
            const glm::vec3 p2(halfExtents.x, halfExtents.y, 0.0f);
            const glm::vec3 p3(-halfExtents.x, halfExtents.y, 0.0f);

            const glm::mat4 M = lightTransform.getTransform();

            asset.positions[0] = glm::vec3(M * glm::vec4(p0, 1.0f));
            asset.positions[1] = glm::vec3(M * glm::vec4(p1, 1.0f));
            asset.positions[2] = glm::vec3(M * glm::vec4(p2, 1.0f));
            asset.positions[3] = glm::vec3(M * glm::vec4(p3, 1.0f));

            // Normal from transform
            const glm::mat3 Nmat = glm::mat3(glm::transpose(glm::inverse(M)));
            const glm::vec3 n    = glm::normalize(Nmat * glm::vec3(0, 0, 1));

            asset.normals[0] = n;
            asset.normals[1] = n;
            asset.normals[2] = n;
            asset.normals[3] = n;

            // UVs
            asset.texCoords0[0] = glm::vec2(0, 0);
            asset.texCoords0[1] = glm::vec2(1, 0);
            asset.texCoords0[2] = glm::vec2(1, 1);
            asset.texCoords0[3] = glm::vec2(0, 1);

            // Indices
            asset.indices = {0, 1, 2, 2, 3, 0};

            // Optional material
            if constexpr (requires { asset.materials; })
            {
                asset.materials.resize(1);

                asset.materials[0].name                   = "AreaLightMaterial";
                asset.materials[0].emissiveColorIntensity = glm::vec4(lightComponent.color, lightComponent.intensity);

                asset.materials[0].doubleSided = lightComponent.twoSided;
            }

            // Create mesh
            return Mesh::create(rd, asset);
        }
    } // namespace gfx
} // namespace vultra
