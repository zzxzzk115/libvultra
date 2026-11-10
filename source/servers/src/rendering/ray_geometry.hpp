#pragma once

#include <vultra/servers/rendering/scene.hpp>

#include <vri/ext/vri_ext_raytracing.h>

#include <optional>

namespace vultra::detail
{
    // Scene-owned BLAS data shared by query policies. Every borrowing TLAS expires before this owner.
    struct RayGeometry
    {
        RayGeometry(Device& device, GpuScene& scene, VriRayTracingInterface ray) :
            device(device),
            scene(scene),
            ray(ray)
        {
        }

        ~RayGeometry();
        RayGeometry(const RayGeometry&)                                = delete;
        RayGeometry&                     operator=(const RayGeometry&) = delete;
        std::expected<void, std::string> initializeExpected();
        std::expected<void, std::string> validateExpected() const;
        std::expected<void, std::string> updateExpected();

        Device&                                   device;
        GpuScene&                                 scene;
        VriRayTracingInterface                    ray;
        VriBuffer*                                vertexBuffer = nullptr;
        VriBuffer*                                indexBuffer  = nullptr;
        std::vector<ScenePrimitive>               primitives;
        std::vector<VriAsGeometryDesc>            geometries;
        std::vector<VriAccelerationStructureDesc> descriptions;
        std::vector<VriAccelerationStructure*>    bottoms;
        std::optional<uint64_t>                   completedRevision;
    };
} // namespace vultra::detail
