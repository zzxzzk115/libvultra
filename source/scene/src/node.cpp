#include <vultra/scene/node.hpp>
#include <vultra/scene/scene_tree.hpp>

#include <stdexcept>
#include <utility>

namespace vultra
{
    NodeId Node::ownerId() const
    {
        return owner() ? owner()->idInScene() : NodeId {};
    }

    void Node::setOwnerId(NodeId id)
    {
        if (!id.value.valid())
        {
            setOwner(nullptr);
            return;
        }
        auto* target = tree() ? tree()->find(id) : nullptr;
        if (!target)
        {
            throw std::invalid_argument("Owner reference does not belong to this world");
        }
        setOwner(target);
    }

    Node::Node(std::string name, NodeId id) :
        m_Detached(std::make_unique<DetachedState>())
    {
        if (!id.value.valid() || name.empty())
        {
            throw std::invalid_argument("Scene node requires a stable ID and name");
        }
        m_Detached->metadata = {this->id(), this, id, std::move(name)};
    }

    Node::~Node() = default;

    SceneNode& Node::metadata()
    {
        return m_Tree ? m_Tree->registry().get<SceneNode>(m_Entity) : m_Detached->metadata;
    }

    const SceneNode& Node::metadata() const
    {
        return m_Tree ? std::as_const(*m_Tree).registry().get<SceneNode>(m_Entity) : m_Detached->metadata;
    }

    SceneProcess& Node::processState()
    {
        return m_Tree ? m_Tree->registry().get<SceneProcess>(m_Entity) : m_Detached->process;
    }

    const SceneProcess& Node::processState() const
    {
        return m_Tree ? std::as_const(*m_Tree).registry().get<SceneProcess>(m_Entity) : m_Detached->process;
    }

    void Node::attachComponents(entt::registry& registry, entt::entity entity)
    {
        registry.emplace<SceneNode>(entity, m_Detached->metadata);
        registry.emplace<SceneProcess>(entity, m_Detached->process);
        m_Detached.reset();
    }

    void Node::detachComponents(entt::registry& registry, entt::entity entity)
    {
        if (!m_Detached)
        {
            m_Detached = std::make_unique<DetachedState>();
        }
        if (const auto* value = registry.try_get<SceneNode>(entity))
        {
            m_Detached->metadata = *value;
        }
        if (const auto* value = registry.try_get<SceneProcess>(entity))
        {
            m_Detached->process               = *value;
            m_Detached->process.behavior      = nullptr;
            m_Detached->process.behaviorReady = false;
        }
    }

    NodeId Node::idInScene() const
    {
        return metadata().persistentId;
    }

    const std::string& Node::name() const
    {
        return metadata().name;
    }

    void Node::setName(std::string name)
    {
        if (name.empty())
        {
            throw std::invalid_argument("Scene node name is empty");
        }
        if (name != metadata().name)
        {
            metadata().name = std::move(name);
            markChanged(SceneChange::eMetadata);
        }
    }

    Node* Node::parent() const
    {
        if (!m_Tree)
        {
            return m_Detached->parent;
        }
        const auto parent = m_Tree->registry().get<SceneHierarchy>(m_Entity).parent;
        return parent == entt::null ? nullptr : m_Tree->registry().get<SceneNode>(parent).node;
    }

    Node* Node::owner() const
    {
        if (!m_Tree)
        {
            return m_Detached->owner;
        }
        const auto owner = m_Tree->registry().get<SceneHierarchy>(m_Entity).owner;
        return owner == entt::null ? nullptr : m_Tree->registry().get<SceneNode>(owner).node;
    }

    void Node::setOwner(Node* owner)
    {
        if (owner)
        {
            auto* ancestor = parent();
            while (ancestor && ancestor != owner)
            {
                ancestor = ancestor->parent();
            }
            if (!ancestor)
            {
                throw std::invalid_argument("Scene owner must be an ancestor of the node");
            }
        }
        if (this->owner() != owner)
        {
            if (m_Tree)
            {
                m_Tree->registry().get<SceneHierarchy>(m_Entity).owner = owner ? owner->entity() : entt::null;
            }
            else
            {
                m_Detached->owner = owner;
            }
            markChanged(SceneChange::eMetadata);
        }
    }

    size_t Node::childCount() const
    {
        return m_Tree ? m_Tree->registry().get<SceneHierarchy>(m_Entity).children.size() : m_Detached->children.size();
    }

    Node* Node::childAt(size_t index) const
    {
        if (!m_Tree)
        {
            return m_Detached->children[index].get();
        }
        const auto child = m_Tree->registry().get<SceneHierarchy>(m_Entity).children[index];
        return m_Tree->registry().get<SceneNode>(child).node;
    }

    NodeKind Node::kind() const
    {
        return NodeKind::eNode;
    }

    std::string_view Node::typeName() const
    {
        return "Node";
    }

    void Node::markChanged(SceneChange change)
    {
        if (m_Tree)
        {
            m_Tree->markChanged(change);
        }
    }

    SceneTree* Node::tree() const
    {
        return m_Tree;
    }

    entt::entity Node::entity() const
    {
        return m_Entity;
    }

    bool Node::isInsideTree() const
    {
        return processState().insideTree;
    }

    bool Node::isReady() const
    {
        return processState().ready && isInsideTree();
    }

    bool Node::isQueuedForDeletion() const
    {
        return processState().queuedForDeletion;
    }

    ProcessMode Node::processMode() const
    {
        return processState().mode;
    }

    void Node::setProcessMode(ProcessMode mode)
    {
        if (mode < ProcessMode::eInherit || mode > ProcessMode::eDisabled)
        {
            throw std::invalid_argument("Invalid node process mode");
        }
        if (processState().mode != mode)
        {
            processState().mode = mode;
            markChanged(SceneChange::eMetadata);
        }
    }

    int32_t Node::processPriority() const
    {
        return processState().priority;
    }

    void Node::setProcessPriority(int32_t priority)
    {
        if (processState().priority != priority)
        {
            processState().priority = priority;
            markChanged(SceneChange::eMetadata);
        }
    }

    bool Node::canProcess() const
    {
        if (!isInsideTree() || isQueuedForDeletion())
        {
            return false;
        }
        return m_Tree->canProcess(m_Entity);
    }

    void Node::queueFree()
    {
        if (!m_Tree)
        {
            throw std::logic_error("Queue deletion requires an attached node");
        }
        m_Tree->queueFree(id());
    }
} // namespace vultra
