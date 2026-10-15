#pragma once

#include <vultra/scene/object.hpp>

#include <entt/entity/entity.hpp>
#include <glm/mat4x4.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace vultra
{
    class Node;
    class NodeBehavior;

    struct NodeId
    {
        StableId    value;
        friend bool operator==(const NodeId&, const NodeId&) = default;
    };

    enum class ProcessMode
    {
        eInherit,
        ePausable,
        eWhenPaused,
        eAlways,
        eDisabled
    };

    enum class NodeKind
    {
        eNode,
        eNode3D,
        eMeshInstance,
        eCamera,
        eLight,
        eEnvironment
    };

    // Runtime-only entities have no Node facade; persistent IDs never contain an EnTT slot or generation.
    struct SceneNode
    {
        ObjectId    id;
        Node*       node = nullptr;
        NodeId      persistentId;
        std::string name;
        size_t      treeOrder = 0;
    };

    struct SceneHierarchy
    {
        entt::entity              parent = entt::null;
        entt::entity              owner  = entt::null;
        std::vector<entt::entity> children;
        uint8_t                   traversalState = 0; // Derived scratch for cycle checks and parent-first ordering.
    };

    // Callback instances are borrowed from their host. All execution state belongs to the world.
    struct SceneProcess
    {
        ProcessMode   mode              = ProcessMode::eInherit;
        int32_t       priority          = 0;
        NodeBehavior* behavior          = nullptr;
        bool          insideTree        = false;
        bool          ready             = false;
        bool          behaviorReady     = false;
        bool          queuedForDeletion = false;
        bool          blocked           = false;
    };

    struct SceneTransform
    {
        glm::mat4   local {1};
        glm::mat4   global {1};
        static void validate(const glm::mat4& transform);
    };

    struct SceneTransformUpdate
    {
        ObjectId  node;
        glm::mat4 local;
    };

    enum class ScenePhase
    {
        ePhysics,
        eProcess
    };

    // World-local callback identity. World moves preserve it; replacing the world retires it.
    struct SceneSystemId
    {
        ObjectId    world;
        uint64_t    value                                    = 0;
        friend bool operator==(SceneSystemId, SceneSystemId) = default;
    };
} // namespace vultra
