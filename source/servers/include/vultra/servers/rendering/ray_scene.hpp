#pragma once

#include <vultra/servers/rendering/scene.hpp>

#include <vri/ext/vri_ext_raytracing.h>

#include <optional>

namespace vultra
{
    struct RaySceneOptions
    {
        // Included blended geometry is non-opaque unless forceOpaque is selected.
        bool includeAlphaBlended = false;
        // Overrides are fixed for the lifetime of the acceleration structure.
        bool forceOpaque        = false;
        bool disableFaceCulling = false;
        // Sends even opaque materials through candidate filtering; mutually exclusive with forceOpaque.
        bool forceNonOpaque = false;
    };

    // A TLAS/query policy over GpuScene's shared BLAS. The scene outlives every policy owner.
    // Transforms, material flags and notified vertex positions update between completed GPU uses.
    // Topology and geometry buffer replacement require a new scene.
    class RayScene
    {
    public:
        static std::expected<std::unique_ptr<RayScene>, std::string>
        create(Device& device, GpuScene& scene, RaySceneOptions options = {});
        ~RayScene();
        RayScene(const RayScene&)            = delete;
        RayScene& operator=(const RayScene&) = delete;

        std::expected<void, std::string> update();

        RaySceneOptions options() const
        {
            return m_Options;
        }

        VriDescriptor* descriptor() const
        {
            return m_Descriptor;
        }

        bool belongsTo(const Device& device, const GpuScene& scene) const
        {
            return &m_Device == &device && &m_Scene == &scene;
        }

    private:
        struct Instance
        {
            float    transform[12] {};
            uint32_t idAndMask      = 0;
            uint32_t offsetAndFlags = 0;
            uint64_t address        = 0;
        };

        static_assert(sizeof(Instance) == 64);

        RayScene(Device&                device,
                 GpuScene&              scene,
                 detail::RayGeometry&   geometry,
                 RaySceneOptions        options,
                 VriRayTracingInterface ray) :
            m_Device(device),
            m_Scene(scene),
            m_Geometry(geometry),
            m_Options(options),
            m_Ray(ray)
        {
        }

        std::expected<void, std::string>          initialize();
        Device&                                   m_Device;
        GpuScene&                                 m_Scene;
        detail::RayGeometry&                      m_Geometry;
        RaySceneOptions                           m_Options;
        VriRayTracingInterface                    m_Ray {};
        std::vector<Instance>                     m_Instances;
        std::vector<Instance>                     m_CompletedInstances;
        std::optional<uint64_t>                   m_CompletedGeometryRevision;
        std::unique_ptr<Buffer>                   m_InstanceBuffer;
        VriAsGeometryDesc                         m_TopGeometry {};
        VriAccelerationStructureDesc              m_TopDescription {};
        VriAccelerationStructure*                 m_Top        = nullptr;
        VriDescriptor*                            m_Descriptor = nullptr;
    };
} // namespace vultra
