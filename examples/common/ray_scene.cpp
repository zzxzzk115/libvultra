#include "ray_scene.hpp"
#include "upload.hpp"

namespace sample
{
    RayScene::RayScene(vultra::Device& device, const vultra::SceneData& scene) :
        m_Device(device)
    {
        vultra::check(vriGetInterface(device.handle, VRI_INTERFACE_RAYTRACING, sizeof(rt), &rt),
                      "Get ray tracing interface");
        std::vector<RayVertex> data;
        data.reserve(scene.indices.size());
        glm::vec3 lightCenter {0};
        glm::vec3 lightDirection {0};
        glm::vec3 emission {0};
        float     lightArea = 0;
        for (const auto& primitive : scene.primitives)
        {
            const auto& material = scene.materials.at(primitive.material);
            if (material.alphaCutoff >= 0 || material.baseColorTexture.image >= 0)
            {
                throw std::invalid_argument("Ray examples require opaque, untextured materials");
            }
            for (uint32_t i = 0; i < primitive.indexCount; ++i)
            {
                const auto& v = scene.vertices.at(scene.indices.at(primitive.firstIndex + i));
                data.push_back({glm::vec4(v.position, 1),
                                glm::vec4(v.normal, 0),
                                v.color * material.baseColor,
                                glm::vec4(material.emissionColor * material.emissionLuminance, 0)});
            }
            if (glm::length(material.emissionColor) > 0)
            {
                for (uint32_t i = 0; i < primitive.indexCount; i += 3)
                {
                    const auto  a      = scene.vertices.at(scene.indices.at(primitive.firstIndex + i)).position;
                    const auto  b      = scene.vertices.at(scene.indices.at(primitive.firstIndex + i + 1)).position;
                    const auto  c      = scene.vertices.at(scene.indices.at(primitive.firstIndex + i + 2)).position;
                    const auto  normal = glm::cross(b - a, c - a);
                    const float area   = glm::length(normal) * 0.5f;
                    lightCenter += (a + b + c) * (area / 3);
                    lightDirection += normal;
                    emission += material.emissionColor * material.emissionLuminance * area;
                    lightArea += area;
                }
            }
        }
        if (lightArea > 0)
        {
            lightPositionArea = glm::vec4(lightCenter / lightArea, lightArea);
            lightNormal       = glm::vec4(glm::normalize(lightDirection), 0);
            lightEmission     = glm::vec4(emission / lightArea, 0);
        }
        vertexCount = uint32_t(data.size());
        vertices    = uploadBuffer(
            device,
            std::as_bytes(std::span(data)),
            VriBufferUsage_VertexBuffer | VriBufferUsage_StorageBuffer | VriBufferUsage_AccelerationBuildInput,
            {VriAccess_AccelerationStructureRead | VriAccess_ShaderResourceRead | VriAccess_VertexBufferRead,
             VriPipelineStage_AllCommands});
        try
        {
            VriBufferViewDesc vertexDesc {vertices->handle,
                                          VriDescriptorType_StructuredBuffer,
                                          VriFormat_Unknown,
                                          0,
                                          0};
            vultra::check(device.core.CreateBufferView(device.handle, &vertexDesc, &vertexView),
                          "Create ray vertex view");
            VriAsGeometryDesc geometry {};
            geometry.type                   = VriAsGeometryType_Triangles;
            geometry.flags                  = VriAsGeometry_Opaque;
            geometry.triangles.vertexBuffer = vertices->handle;
            geometry.triangles.vertexCount  = vertexCount;
            geometry.triangles.vertexStride = sizeof(RayVertex);
            geometry.triangles.vertexFormat = VriFormat_RGB32_SFLOAT;
            VriAccelerationStructureDesc bottom {VriAccelerationStructureType_BottomLevel,
                                                 VriAccelerationStructureBuild_PreferFastTrace,
                                                 1,
                                                 &geometry};
            vultra::check(rt.CreateAccelerationStructure(device.handle, &bottom, &m_Blas), "Create BLAS");

            struct Instance
            {
                float    transform[12] {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
                uint32_t idAndMask         = 0xffu << 24;
                uint32_t sbtOffsetAndFlags = 1u << 24; // Disable triangle facing culling.
                uint64_t address;
            } instance;

            static_assert(sizeof(Instance) == 64);
            instance.address = rt.GetAccelerationStructureDeviceAddress(m_Blas);
            auto instances =
                uploadBuffer(device,
                             std::as_bytes(std::span(&instance, 1)),
                             VriBufferUsage_AccelerationBuildInput,
                             {VriAccess_AccelerationStructureRead, VriPipelineStage_AccelerationStructureBuild});
            VriAsGeometryDesc instanceGeometry {};
            instanceGeometry.type                     = VriAsGeometryType_Instances;
            instanceGeometry.instances.instanceBuffer = instances->handle;
            instanceGeometry.instances.instanceCount  = 1;
            VriAccelerationStructureDesc top {VriAccelerationStructureType_TopLevel,
                                              VriAccelerationStructureBuild_PreferFastTrace,
                                              1,
                                              &instanceGeometry};
            vultra::check(rt.CreateAccelerationStructure(device.handle, &top, &m_Tlas), "Create TLAS");
            vultra::Frame                           frame(device);
            auto*                                   cmd = frame.begin();
            const VriBuildAccelerationStructureDesc buildBottom {m_Blas, &bottom};
            const VriBuildAccelerationStructureDesc buildTop {m_Tlas, &top};
            // VRI emits the AS-build dependency barrier after each build.
            rt.CmdBuildAccelerationStructure(cmd, &buildBottom);
            rt.CmdBuildAccelerationStructure(cmd, &buildTop);
            frame.submitAndWait();
            vultra::check(rt.CreateAccelerationStructureDescriptor(device.handle, m_Tlas, &sceneView),
                          "Create TLAS view");
        }
        catch (...)
        {
            release();
            throw;
        }
    }

    RayScene::~RayScene()
    {
        m_Device.waitIdle();
        release();
    }

    void RayScene::release()
    {
        if (sceneView)
        {
            m_Device.core.DestroyDescriptor(sceneView);
        }
        if (vertexView)
        {
            m_Device.core.DestroyDescriptor(vertexView);
        }
        if (m_Tlas)
        {
            rt.DestroyAccelerationStructure(m_Tlas);
        }
        if (m_Blas)
        {
            rt.DestroyAccelerationStructure(m_Blas);
        }
    }
} // namespace sample
