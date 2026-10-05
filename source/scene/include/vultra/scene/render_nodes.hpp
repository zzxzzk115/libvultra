#pragma once

#include <vultra/core/base/api_annotations.hpp>
#include <vultra/scene/node.hpp>
#include <vultra/servers/rendering/builtin/render_light.hpp>

namespace vultra
{
    struct VULTRA_REFLECT VULTRA_BIND_POD EnvironmentSettings
    {
        VULTRA_PROPERTY("label=Intensity;min=0;max=4")
        float       intensity                                                          = 1;
        friend bool operator==(const EnvironmentSettings&, const EnvironmentSettings&) = default;
    };

    // Non-spatial scene environment. An invalid asset ID selects the analytic studio environment.
    class EnvironmentNode final : public Node
    {
    public:
        explicit EnvironmentNode(std::string name, NodeId id = {StableId::generate()});
        NodeKind                   kind() const override;
        AssetId                    radianceAsset() const;
        void                       setRadianceAsset(AssetId asset);
        const EnvironmentSettings& settings() const;
        void                       setSettings(EnvironmentSettings settings);

    private:
        AssetId             m_RadianceAsset {};
        EnvironmentSettings m_Settings;
    };

    struct VULTRA_REFLECT VULTRA_BIND_POD CameraSettings
    {
        VULTRA_PROPERTY("label=FOV (rad);min=0.01;max=3.13")
        float verticalFov = 1.04719755f; // Radians; VRI's right-handed, Y-up, zero-to-one projection.
        VULTRA_PROPERTY("label=Near plane;min=0.0001;max=1000000;widget=drag;speed=0.01")
        float nearPlane = 0.01f;
        VULTRA_PROPERTY("label=Far plane;min=0.0001;max=1000000;widget=drag;speed=0.1")
        float       farPlane                                                 = 100;
        friend bool operator==(const CameraSettings&, const CameraSettings&) = default;
    };

    struct VULTRA_REFLECT VULTRA_BIND_POD LightSettings
    {
        VULTRA_PROPERTY("label=Red;min=0;max=4")
        float red = 1;
        VULTRA_PROPERTY("label=Green;min=0;max=4")
        float green = 1;
        VULTRA_PROPERTY("label=Blue;min=0;max=4")
        float blue = 1;
        VULTRA_PROPERTY("label=Intensity;min=0;max=100;widget=drag;speed=0.1")
        float intensity = 1;
        VULTRA_PROPERTY("label=Range;min=0.001;max=1000000;widget=drag;speed=0.1")
        float range = 10;
        VULTRA_PROPERTY("label=Inner cone (rad);min=0;max=1.56")
        float innerCone = 0.35f;
        VULTRA_PROPERTY("label=Outer cone (rad);min=0.001;max=1.56")
        float       outerCone                                              = 0.6f;
        friend bool operator==(const LightSettings&, const LightSettings&) = default;
    };

    class CameraNode final : public Node
    {
    public:
        explicit CameraNode(std::string name, NodeId id = {StableId::generate()});
        NodeKind              kind() const override;
        const CameraSettings& settings() const;
        void                  setSettings(CameraSettings settings);

    private:
        CameraSettings m_Settings;
    };

    class LightNode final : public Node
    {
    public:
        LightNode(std::string name, RenderLightKind lightKind, NodeId id = {StableId::generate()});
        NodeKind             kind() const override;
        RenderLightKind      lightKind() const;
        const LightSettings& settings() const;
        void                 setSettings(LightSettings settings);

    private:
        RenderLightKind m_LightKind;
        LightSettings   m_Settings;
    };
} // namespace vultra
