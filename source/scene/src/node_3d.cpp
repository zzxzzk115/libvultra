#include <vultra/scene/node_3d.hpp>
#include <vultra/scene/scene_tree.hpp>

#include <glm/gtc/matrix_inverse.hpp>

#include <cmath>
#include <stdexcept>
#include <utility>

namespace vultra
{
    Node3D::Node3D(std::string name, NodeId id) :
        Node(std::move(name), id)
    {
    }

    void Node3D::attachComponents(entt::registry& registry, entt::entity entity)
    {
        registry.emplace<SceneTransform>(entity, *m_DetachedTransform);
        Node::attachComponents(registry, entity);
        m_DetachedTransform.reset();
    }

    void Node3D::detachComponents(entt::registry& registry, entt::entity entity)
    {
        if (const auto* transform = registry.try_get<SceneTransform>(entity))
        {
            m_DetachedTransform = *transform;
        }
        Node::detachComponents(registry, entity);
    }

    NodeKind Node3D::kind() const
    {
        return NodeKind::eNode3D;
    }

    std::string_view Node3D::typeName() const
    {
        return "Node3D";
    }

    const glm::mat4& Node3D::localTransform() const
    {
        if (tree())
        {
            return tree()->registry().get<SceneTransform>(entity()).local;
        }
        return m_DetachedTransform->local;
    }

    void Node3D::setLocalTransform(const glm::mat4& transform)
    {
        if (tree())
        {
            const SceneTransformUpdate update {id(), transform};
            tree()->setLocalTransforms({&update, 1});
            return;
        }
        SceneTransform::validate(transform);
        m_DetachedTransform->local = transform;
    }

    Node3D* Node3D::parentNode3D() const
    {
        return dynamic_cast<Node3D*>(parent());
    }

    glm::mat4 Node3D::globalTransform() const
    {
        if (tree())
        {
            tree()->syncTransforms();
            return tree()->registry().get<SceneTransform>(entity()).global;
        }
        const auto* spatialParent = parentNode3D();
        return spatialParent ? spatialParent->globalTransform() * localTransform() : localTransform();
    }

    void Node3D::setGlobalTransform(const glm::mat4& transform)
    {
        const auto* spatialParent = parentNode3D();
        setLocalTransform(spatialParent ? localForParent(transform, spatialParent->globalTransform()) : transform);
    }

    glm::mat4 Node3D::localForParent(const glm::mat4& global, const glm::mat4& parent)
    {
        const auto determinant = glm::determinant(parent);
        if (!std::isfinite(determinant) || determinant == 0)
        {
            throw std::invalid_argument("Cannot preserve a Node3D world transform under a singular parent");
        }
        const auto local = glm::inverse(parent) * global;
        SceneTransform::validate(local);
        return local;
    }
} // namespace vultra
