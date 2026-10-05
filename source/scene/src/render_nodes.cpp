#include <vultra/scene/render_nodes.hpp>

#include <cmath>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace vultra
{
    EnvironmentNode::EnvironmentNode(std::string name, NodeId id) :
        Node(std::move(name), id)
    {
    }

    NodeKind EnvironmentNode::kind() const
    {
        return NodeKind::eEnvironment;
    }

    AssetId EnvironmentNode::radianceAsset() const
    {
        return m_RadianceAsset;
    }

    void EnvironmentNode::setRadianceAsset(AssetId asset)
    {
        if (asset != m_RadianceAsset)
        {
            m_RadianceAsset = asset;
            markChanged(SceneChange::eEnvironment);
        }
    }

    const EnvironmentSettings& EnvironmentNode::settings() const
    {
        return m_Settings;
    }

    void EnvironmentNode::setSettings(EnvironmentSettings settings)
    {
        if (!std::isfinite(settings.intensity) || settings.intensity < 0)
        {
            throw std::invalid_argument("Environment requires a nonnegative finite intensity");
        }
        if (settings != m_Settings)
        {
            m_Settings = settings;
            markChanged(SceneChange::eEnvironment);
        }
    }

    CameraNode::CameraNode(std::string name, NodeId id) :
        Node(std::move(name), id)
    {
    }

    NodeKind CameraNode::kind() const
    {
        return NodeKind::eCamera;
    }

    const CameraSettings& CameraNode::settings() const
    {
        return m_Settings;
    }

    void CameraNode::setSettings(CameraSettings settings)
    {
        if (!std::isfinite(settings.verticalFov) || settings.verticalFov <= 0 ||
            settings.verticalFov >= std::numbers::pi_v<float> || !std::isfinite(settings.nearPlane) ||
            !std::isfinite(settings.farPlane) || settings.nearPlane <= 0 || settings.farPlane <= settings.nearPlane)
        {
            throw std::invalid_argument("Camera requires a finite FOV in (0, pi) and 0 < near < far");
        }
        if (settings != m_Settings)
        {
            m_Settings = settings;
            markChanged(SceneChange::eCamera);
        }
    }

    LightNode::LightNode(std::string name, RenderLightKind lightKind, NodeId id) :
        Node(std::move(name), id),
        m_LightKind(lightKind)
    {
        if (lightKind > RenderLightKind::eSpot)
        {
            throw std::invalid_argument("Unknown light kind");
        }
    }

    NodeKind LightNode::kind() const
    {
        return NodeKind::eLight;
    }

    RenderLightKind LightNode::lightKind() const
    {
        return m_LightKind;
    }

    const LightSettings& LightNode::settings() const
    {
        return m_Settings;
    }

    void LightNode::setSettings(LightSettings settings)
    {
        if (!std::isfinite(settings.red) || !std::isfinite(settings.green) || !std::isfinite(settings.blue) ||
            settings.red < 0 || settings.green < 0 || settings.blue < 0 || !std::isfinite(settings.intensity) ||
            settings.intensity < 0 || !std::isfinite(settings.range) || settings.range <= 0 ||
            !std::isfinite(settings.innerCone) || !std::isfinite(settings.outerCone) || settings.innerCone < 0 ||
            settings.innerCone >= settings.outerCone || settings.outerCone >= std::numbers::pi_v<float> * 0.5f)
        {
            throw std::invalid_argument("Light requires nonnegative finite color/intensity, positive range and "
                                        "0 <= inner cone < outer cone < pi/2");
        }
        if (settings != m_Settings)
        {
            m_Settings = settings;
            markChanged(SceneChange::eLighting);
        }
    }
} // namespace vultra
