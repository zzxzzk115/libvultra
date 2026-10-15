#pragma once

#include <vultra/assets/project_manifest.hpp>
#include <vultra/core/base/api_annotations.hpp>
#include <vultra/servers/rendering/builtin/render_light.hpp>

#include <vector>

namespace vultra
{
    struct MeshMaterialOverride
    {
        uint32_t slot;
        AssetId  material;
    };

    struct VULTRA_REFLECT VULTRA_BIND_POD EnvironmentSettings
    {
        VULTRA_PROPERTY("label=Intensity;min=0;max=4;json=/intensity")
        float       intensity                                                          = 1;
        friend bool operator==(const EnvironmentSettings&, const EnvironmentSettings&) = default;
    };

    struct VULTRA_REFLECT VULTRA_BIND_POD CameraSettings
    {
        VULTRA_PROPERTY("label=FOV (rad);min=0.01;max=3.13;json=/vertical_fov")
        float verticalFov = 1.04719755f; // Radians; VRI's right-handed, Y-up, zero-to-one projection.
        VULTRA_PROPERTY("label=Near plane;min=0.0001;max=1000000;widget=drag;speed=0.01;json=/near;api=NearPlane")
        float nearPlane = 0.01f;
        VULTRA_PROPERTY("label=Far plane;min=0.0001;max=1000000;widget=drag;speed=0.1;json=/far;api=FarPlane")
        float       farPlane                                                 = 100;
        friend bool operator==(const CameraSettings&, const CameraSettings&) = default;
    };

    struct VULTRA_REFLECT VULTRA_BIND_POD LightSettings
    {
        VULTRA_PROPERTY("label=Red;min=0;max=4;json=/color/0")
        float red = 1;
        VULTRA_PROPERTY("label=Green;min=0;max=4;json=/color/1")
        float green = 1;
        VULTRA_PROPERTY("label=Blue;min=0;max=4;json=/color/2")
        float blue = 1;
        VULTRA_PROPERTY("label=Intensity;min=0;max=100;widget=drag;speed=0.1;json=/intensity")
        float intensity = 1;
        VULTRA_PROPERTY("label=Range;min=0.001;max=1000000;widget=drag;speed=0.1;json=/range")
        float range = 10;
        VULTRA_PROPERTY("label=Inner cone (rad);min=0;max=1.56;json=/inner_cone")
        float innerCone = 0.35f;
        VULTRA_PROPERTY("label=Outer cone (rad);min=0.001;max=1.56;json=/outer_cone")
        float       outerCone                                              = 0.6f;
        friend bool operator==(const LightSettings&, const LightSettings&) = default;
    };

    struct SceneMesh
    {
        AssetId                           model;
        std::vector<MeshMaterialOverride> materials;
    };

    struct SceneCamera
    {
        CameraSettings settings;
        static void    validate(const CameraSettings& settings);
    };

    struct SceneLight
    {
        RenderLightKind kind = RenderLightKind::eDirectional;
        LightSettings   settings;
        static void     validate(const LightSettings& settings);
    };

    struct SceneEnvironment
    {
        AssetId             radiance {};
        EnvironmentSettings settings;
        static void         validate(const EnvironmentSettings& settings);
    };
} // namespace vultra
