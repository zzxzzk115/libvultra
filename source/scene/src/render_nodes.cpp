#include <vultra/scene/render_nodes.hpp>
#include <vultra/scene/scene_tree.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace vultra
{
    std::string_view WorldEnvironment::typeName() const
    {
        return "WorldEnvironment";
    }

    std::string_view Camera3D::typeName() const
    {
        return "Camera3D";
    }

    std::string_view Light3D::typeName() const
    {
        switch (data().kind)
        {
            case RenderLightKind::eDirectional:
                return "DirectionalLight3D";
            case RenderLightKind::ePoint:
                return "OmniLight3D";
            case RenderLightKind::eSpot:
                return "SpotLight3D";
        }
        throw std::logic_error("Unknown light kind");
    }

    std::string_view MeshInstance3D::typeName() const
    {
        return "MeshInstance3D";
    }

    WorldEnvironment::WorldEnvironment(std::string name, NodeId id) :
        Node(std::move(name), id)
    {
    }

    NodeKind WorldEnvironment::kind() const
    {
        return NodeKind::eEnvironment;
    }

    AssetId WorldEnvironment::radianceAsset() const
    {
        return data().radiance;
    }

    void WorldEnvironment::setRadianceAsset(AssetId asset)
    {
        if (asset != data().radiance)
        {
            data().radiance = asset;
            markChanged(SceneChange::eEnvironment);
        }
    }

    const EnvironmentSettings& WorldEnvironment::settings() const
    {
        return data().settings;
    }

    void SceneEnvironment::validate(const EnvironmentSettings& settings)
    {
        if (!std::isfinite(settings.intensity) || settings.intensity < 0)
        {
            throw std::invalid_argument("Environment requires a nonnegative finite intensity");
        }
    }

    void WorldEnvironment::setSettings(EnvironmentSettings settings)
    {
        SceneEnvironment::validate(settings);
        if (settings != data().settings)
        {
            data().settings = settings;
            markChanged(SceneChange::eEnvironment);
        }
    }

    Camera3D::Camera3D(std::string name, NodeId id) :
        Node3D(std::move(name), id)
    {
    }

    NodeKind Camera3D::kind() const
    {
        return NodeKind::eCamera;
    }

    const CameraSettings& Camera3D::settings() const
    {
        return data().settings;
    }

    void SceneCamera::validate(const CameraSettings& settings)
    {
        if (!std::isfinite(settings.verticalFov) || settings.verticalFov <= 0 ||
            settings.verticalFov >= std::numbers::pi_v<float> || !std::isfinite(settings.nearPlane) ||
            !std::isfinite(settings.farPlane) || settings.nearPlane <= 0 || settings.farPlane <= settings.nearPlane)
        {
            throw std::invalid_argument("Camera requires a finite FOV in (0, pi) and 0 < near < far");
        }
    }

    void Camera3D::setSettings(CameraSettings settings)
    {
        SceneCamera::validate(settings);
        if (settings != data().settings)
        {
            data().settings = settings;
            markChanged(SceneChange::eCamera);
        }
    }

    Light3D::Light3D(std::string name, RenderLightKind lightKind, NodeId id) :
        Node3D(std::move(name), id),
        m_DetachedLight(SceneLight {lightKind, {}})
    {
        if (lightKind > RenderLightKind::eSpot)
        {
            throw std::invalid_argument("Unknown light kind");
        }
    }

    NodeKind Light3D::kind() const
    {
        return NodeKind::eLight;
    }

    RenderLightKind Light3D::lightKind() const
    {
        return data().kind;
    }

    const LightSettings& Light3D::settings() const
    {
        return data().settings;
    }

    void SceneLight::validate(const LightSettings& settings)
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
    }

    void Light3D::setSettings(LightSettings settings)
    {
        SceneLight::validate(settings);
        if (settings != data().settings)
        {
            data().settings = settings;
            markChanged(SceneChange::eLighting);
        }
    }

    MeshInstance3D::MeshInstance3D(std::string name, AssetId model, NodeId id) :
        Node3D(std::move(name), id),
        m_DetachedMesh(SceneMesh {model, {}})
    {
        if (!model.value.valid())
        {
            throw std::invalid_argument("Mesh instance requires a model asset ID");
        }
    }

    NodeKind MeshInstance3D::kind() const
    {
        return NodeKind::eMeshInstance;
    }

    AssetId MeshInstance3D::model() const
    {
        return data().model;
    }

    void MeshInstance3D::setModel(AssetId model)
    {
        if (!model.value.valid())
        {
            throw std::invalid_argument("Mesh instance model asset ID is empty");
        }
        if (model != data().model)
        {
            data().model = model;
            markChanged(SceneChange::eStructure);
        }
    }

    const std::vector<MeshMaterialOverride>& MeshInstance3D::materialOverrides() const
    {
        return data().materials;
    }

    void MeshInstance3D::setMaterial(uint32_t slot, AssetId material)
    {
        const auto found = std::ranges::find(data().materials, slot, &MeshMaterialOverride::slot);
        if (!material.value.valid())
        {
            if (found != data().materials.end())
            {
                data().materials.erase(found);
                markChanged(SceneChange::eMaterial);
            }
        }
        else if (found != data().materials.end())
        {
            if (found->material != material)
            {
                found->material = material;
                markChanged(SceneChange::eMaterial);
            }
        }
        else
        {
            data().materials.push_back({slot, material});
            std::ranges::sort(data().materials, {}, &MeshMaterialOverride::slot);
            markChanged(SceneChange::eMaterial);
        }
    }

    SceneMesh& MeshInstance3D::data()
    {
        return tree() ? tree()->registry().get<SceneMesh>(entity()) : *m_DetachedMesh;
    }

    const SceneMesh& MeshInstance3D::data() const
    {
        return tree() ? std::as_const(*tree()).registry().get<SceneMesh>(entity()) : *m_DetachedMesh;
    }

    void MeshInstance3D::attachComponents(entt::registry& registry, entt::entity entity)
    {
        registry.emplace<SceneMesh>(entity, std::move(*m_DetachedMesh));
        Node3D::attachComponents(registry, entity);
        m_DetachedMesh.reset();
    }

    void MeshInstance3D::detachComponents(entt::registry& registry, entt::entity entity)
    {
        if (auto* value = registry.try_get<SceneMesh>(entity))
        {
            m_DetachedMesh = std::move(*value);
        }
        Node3D::detachComponents(registry, entity);
    }

    SceneCamera& Camera3D::data()
    {
        return tree() ? tree()->registry().get<SceneCamera>(entity()) : *m_DetachedCamera;
    }

    const SceneCamera& Camera3D::data() const
    {
        return tree() ? std::as_const(*tree()).registry().get<SceneCamera>(entity()) : *m_DetachedCamera;
    }

    void Camera3D::attachComponents(entt::registry& registry, entt::entity entity)
    {
        registry.emplace<SceneCamera>(entity, std::move(*m_DetachedCamera));
        Node3D::attachComponents(registry, entity);
        m_DetachedCamera.reset();
    }

    void Camera3D::detachComponents(entt::registry& registry, entt::entity entity)
    {
        if (auto* value = registry.try_get<SceneCamera>(entity))
        {
            m_DetachedCamera = std::move(*value);
        }
        Node3D::detachComponents(registry, entity);
    }

    SceneLight& Light3D::data()
    {
        return tree() ? tree()->registry().get<SceneLight>(entity()) : *m_DetachedLight;
    }

    const SceneLight& Light3D::data() const
    {
        return tree() ? std::as_const(*tree()).registry().get<SceneLight>(entity()) : *m_DetachedLight;
    }

    void Light3D::attachComponents(entt::registry& registry, entt::entity entity)
    {
        registry.emplace<SceneLight>(entity, std::move(*m_DetachedLight));
        Node3D::attachComponents(registry, entity);
        m_DetachedLight.reset();
    }

    void Light3D::detachComponents(entt::registry& registry, entt::entity entity)
    {
        if (auto* value = registry.try_get<SceneLight>(entity))
        {
            m_DetachedLight = std::move(*value);
        }
        Node3D::detachComponents(registry, entity);
    }

    SceneEnvironment& WorldEnvironment::data()
    {
        return tree() ? tree()->registry().get<SceneEnvironment>(entity()) : *m_DetachedEnvironment;
    }

    const SceneEnvironment& WorldEnvironment::data() const
    {
        return tree() ? std::as_const(*tree()).registry().get<SceneEnvironment>(entity()) : *m_DetachedEnvironment;
    }

    void WorldEnvironment::attachComponents(entt::registry& registry, entt::entity entity)
    {
        registry.emplace<SceneEnvironment>(entity, std::move(*m_DetachedEnvironment));
        Node::attachComponents(registry, entity);
        m_DetachedEnvironment.reset();
    }

    void WorldEnvironment::detachComponents(entt::registry& registry, entt::entity entity)
    {
        if (auto* value = registry.try_get<SceneEnvironment>(entity))
        {
            m_DetachedEnvironment = std::move(*value);
        }
        Node::detachComponents(registry, entity);
    }

} // namespace vultra
