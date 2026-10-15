#pragma once

#include <vultra/scene/node.hpp>
#include <vultra/scene/scene_components.hpp>

#include <glm/mat4x4.hpp>

#include <optional>

namespace vultra
{
    class VULTRA_REFLECT_NODE Node3D : public Node
    {
    public:
        explicit Node3D(std::string name, NodeId id = {StableId::generate()});
        NodeKind         kind() const override;
        std::string_view typeName() const override;
        VULTRA_PROPERTY(
            "label=Local "
            "transform;json=/transform;set=setLocalTransform;default=glm::mat4{1};flags=serialize|inspect|bind")
        const glm::mat4& localTransform() const;
        void             setLocalTransform(const glm::mat4& transform);
        glm::mat4        globalTransform() const;
        void             setGlobalTransform(const glm::mat4& transform);
        // A non-spatial direct parent breaks transform inheritance, as in Godot Node3D.
        Node3D* parentNode3D() const;

    protected:
        void attachComponents(entt::registry& registry, entt::entity entity) override;
        void detachComponents(entt::registry& registry, entt::entity entity) override;

    private:
        friend class SceneTree;
        static glm::mat4 localForParent(const glm::mat4& global, const glm::mat4& parent);
        // Only detached nodes own this value; attachment transfers it into the world's component store.
        std::optional<SceneTransform> m_DetachedTransform {SceneTransform {}};
    };
} // namespace vultra
