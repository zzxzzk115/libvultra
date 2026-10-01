#include <vultra/scene/scene_api.hpp>

#include <stdexcept>

namespace vultra
{
    namespace
    {
        const Node& requireNode(const SceneTree& scene, ObjectId id)
        {
            const auto* node = scene.find(id);
            if (!node)
            {
                throw std::invalid_argument("ObjectId does not belong to this scene");
            }
            return *node;
        }
    } // namespace

    ObjectId sceneRootId(const SceneTree& scene)
    {
        return scene.root().id();
    }

    uint64_t sceneChildCount(const SceneTree& scene, ObjectId parent)
    {
        return requireNode(scene, parent).children().size();
    }

    ObjectId sceneChildId(const SceneTree& scene, ObjectId parent, uint64_t index)
    {
        const auto& children = requireNode(scene, parent).children();
        if (index >= children.size())
        {
            throw std::invalid_argument("Scene child index is out of range");
        }
        return children[size_t(index)]->id();
    }

    std::string_view sceneNodeName(const SceneTree& scene, ObjectId node)
    {
        return requireNode(scene, node).name();
    }

    SceneTranslation sceneNodeTranslation(const SceneTree& scene, ObjectId node)
    {
        const auto& transform = requireNode(scene, node).localTransform();
        return {transform[3].x, transform[3].y, transform[3].z};
    }

    void sceneSetNodeTranslation(SceneTree& scene, ObjectId node, SceneTranslation translation)
    {
        auto* target = scene.find(node);
        if (!target)
        {
            throw std::invalid_argument("ObjectId does not belong to this scene");
        }
        auto transform = target->localTransform();
        transform[3].x = translation.x;
        transform[3].y = translation.y;
        transform[3].z = translation.z;
        target->setLocalTransform(transform);
    }
} // namespace vultra
