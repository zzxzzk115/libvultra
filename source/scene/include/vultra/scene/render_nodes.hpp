#pragma once

#include <vultra/scene/node_3d.hpp>
#include <vultra/scene/render_components.hpp>

#include <optional>

namespace vultra
{
    class VULTRA_REFLECT_NODE MeshInstance3D final : public Node3D
    {
    public:
        MeshInstance3D(std::string name, AssetId model, NodeId id = {StableId::generate()});
        NodeKind         kind() const override;
        std::string_view typeName() const override;
        VULTRA_PROPERTY(
            "label=Model;set=setModel;default=AssetId{};flags=inspect|bind;reference=ModelResource;nullable=false")
        AssetId                                  model() const;
        void                                     setModel(AssetId model);
        const std::vector<MeshMaterialOverride>& materialOverrides() const;
        // An invalid asset ID clears the override, restoring the imported model's parameters.
        void setMaterial(uint32_t slot, AssetId material);

    private:
        void                     attachComponents(entt::registry& registry, entt::entity entity) override;
        void                     detachComponents(entt::registry& registry, entt::entity entity) override;
        SceneMesh&               data();
        const SceneMesh&         data() const;
        std::optional<SceneMesh> m_DetachedMesh;
    };

    // Non-spatial scene environment. An invalid asset ID selects the analytic studio environment.
    class VULTRA_REFLECT_NODE WorldEnvironment final : public Node
    {
    public:
        explicit WorldEnvironment(std::string name, NodeId id = {StableId::generate()});
        NodeKind         kind() const override;
        std::string_view typeName() const override;
        VULTRA_PROPERTY("label=Radiance;set=setRadianceAsset;default=AssetId{};flags=inspect|bind;reference=Resource")
        AssetId                    radianceAsset() const;
        void                       setRadianceAsset(AssetId asset);
        const EnvironmentSettings& settings() const;
        void                       setSettings(EnvironmentSettings settings);

    private:
        void                            attachComponents(entt::registry& registry, entt::entity entity) override;
        void                            detachComponents(entt::registry& registry, entt::entity entity) override;
        SceneEnvironment&               data();
        const SceneEnvironment&         data() const;
        std::optional<SceneEnvironment> m_DetachedEnvironment {SceneEnvironment {}};
    };

    class VULTRA_REFLECT_NODE Camera3D final : public Node3D
    {
    public:
        explicit Camera3D(std::string name, NodeId id = {StableId::generate()});
        NodeKind              kind() const override;
        std::string_view      typeName() const override;
        const CameraSettings& settings() const;
        void                  setSettings(CameraSettings settings);

    private:
        void                       attachComponents(entt::registry& registry, entt::entity entity) override;
        void                       detachComponents(entt::registry& registry, entt::entity entity) override;
        SceneCamera&               data();
        const SceneCamera&         data() const;
        std::optional<SceneCamera> m_DetachedCamera {SceneCamera {}};
    };

    class VULTRA_REFLECT_NODE Light3D final : public Node3D
    {
    public:
        Light3D(std::string name, RenderLightKind lightKind, NodeId id = {StableId::generate()});
        NodeKind             kind() const override;
        std::string_view     typeName() const override;
        RenderLightKind      lightKind() const;
        const LightSettings& settings() const;
        void                 setSettings(LightSettings settings);

    private:
        void                      attachComponents(entt::registry& registry, entt::entity entity) override;
        void                      detachComponents(entt::registry& registry, entt::entity entity) override;
        SceneLight&               data();
        const SceneLight&         data() const;
        std::optional<SceneLight> m_DetachedLight;
    };
} // namespace vultra
