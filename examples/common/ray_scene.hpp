#pragma once

#include <vultra/servers/rendering/scene.hpp>

#include <vri/ext/vri_ext_raytracing.h>

namespace sample
{
    struct RayVertex
    {
        glm::vec4 position;
        glm::vec4 normal;
        glm::vec4 color;
        glm::vec4 emission;
    };

    static_assert(sizeof(RayVertex) == 64);

    // Static, opaque example geometry. One BLAS and one identity instance in the TLAS.
    // The same vertex buffer feeds rasterization, AS construction and closest-hit shading.
    class RayScene
    {
    public:
        RayScene(vultra::Device& device, const vultra::SceneData& scene);
        ~RayScene();
        RayScene(const RayScene&)            = delete;
        RayScene& operator=(const RayScene&) = delete;

        VriRayTracingInterface          rt {};
        std::unique_ptr<vultra::Buffer> vertices;
        uint32_t                        vertexCount = 0;
        VriDescriptor*                  sceneView   = nullptr;
        VriDescriptor*                  vertexView  = nullptr;
        glm::vec4                       lightPositionArea {0};
        glm::vec4                       lightNormal {0};
        glm::vec4                       lightEmission {0};

    private:
        void                      release();
        vultra::Device&           m_Device;
        VriAccelerationStructure* m_Blas = nullptr;
        VriAccelerationStructure* m_Tlas = nullptr;
    };
} // namespace sample
