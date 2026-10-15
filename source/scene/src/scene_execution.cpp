#include <vultra/core/base/logger.hpp>
#include <vultra/scene/node_3d.hpp>
#include <vultra/scene/scene_tree.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <utility>

namespace vultra
{
    void SceneTree::bindTree(Node& node, SceneTree* tree)
    {
        if (!tree)
        {
            const auto handle    = node.m_Entity;
            const auto hierarchy = m_Registry->get<SceneHierarchy>(handle);
            Node* parent = hierarchy.parent == entt::null ? nullptr : m_Registry->get<SceneNode>(hierarchy.parent).node;
            Node* owner  = hierarchy.owner == entt::null ? nullptr : m_Registry->get<SceneNode>(hierarchy.owner).node;
            std::vector<std::unique_ptr<Node>> children;
            children.reserve(hierarchy.children.size());
            for (const auto childEntity : hierarchy.children)
            {
                const auto childId = m_Registry->get<SceneNode>(childEntity).id;
                auto       owned   = m_Nodes.extract(childId.value);
                auto       child   = std::move(owned.mapped());
                bindTree(*child, nullptr);
                children.push_back(std::move(child));
            }
            node.detachComponents(*m_Registry, handle);
            node.m_Detached->parent   = parent;
            node.m_Detached->owner    = owner;
            node.m_Detached->children = std::move(children);
            m_PersistentEntities.erase(node.m_Detached->metadata.persistentId.value.bytes);
            m_NodeEntities.erase(node.id().value);
            node.m_Entity = entt::null;
            node.m_Tree   = nullptr;
            m_Registry->destroy(handle);
            return;
        }

        auto       children      = std::move(node.m_Detached->children);
        const auto parent        = node.m_Detached->parent;
        const auto owner         = node.m_Detached->owner;
        const auto persistent    = node.idInScene();
        const auto sceneLighting = m_SceneLighting;
        const auto handle        = m_Registry->create();
        try
        {
            m_Registry->emplace<SceneHierarchy>(handle,
                                                parent ? parent->entity() : entt::null,
                                                owner ? owner->entity() : entt::null);
            node.attachComponents(*m_Registry, handle);
            m_NodeEntities.emplace(node.id().value, handle);
            m_PersistentEntities.emplace(persistent.value.bytes, handle);
            node.m_Entity = handle;
            node.m_Tree   = tree;
            m_Registry->get<SceneHierarchy>(handle).children.reserve(children.size());
            for (auto& child : children)
            {
                auto* pointer = child.get();
                // Allocate ownership before publishing a live child entity; recursive attachment can rehash the map.
                const auto childId = pointer->id().value;
                m_Nodes.try_emplace(childId, std::move(child));
                try
                {
                    bindTree(*pointer, tree);
                }
                catch (...)
                {
                    child = std::move(m_Nodes.extract(childId).mapped());
                    throw;
                }
                m_Registry->get<SceneHierarchy>(handle).children.push_back(pointer->entity());
            }
        }
        catch (...)
        {
            if (node.m_Entity != entt::null)
            {
                bindTree(node, nullptr);
            }
            else
            {
                node.detachComponents(*m_Registry, handle);
                m_NodeEntities.erase(node.id().value);
                m_PersistentEntities.erase(persistent.value.bytes);
                m_Registry->destroy(handle);
            }
            for (auto& child : children)
            {
                if (child)
                {
                    node.m_Detached->children.push_back(std::move(child));
                }
            }
            node.m_Detached->parent = parent;
            node.m_Detached->owner  = owner;
            m_SceneLighting         = sceneLighting;
            throw;
        }
    }

    void SceneTree::requireIdle() const
    {
        if (m_Dispatching || m_Flushing)
        {
            throw std::logic_error("Scene execution is not reentrant");
        }
    }

    void SceneTree::requireMovable() const
    {
        requireIdle();
        if (m_Running)
        {
            throw std::logic_error("Stop the scene before moving or replacing it");
        }
        if (!m_Registry)
        {
            return;
        }
        for (const auto& [entity, process] : m_Registry->view<const SceneProcess>().each())
        {
            if (process.behavior)
            {
                throw std::logic_error("Detach script hosts before moving or replacing the scene");
            }
        }
    }

    void SceneTree::requireStructuralBoundary(const Node* parent) const
    {
        if (m_Dispatching && (!m_Lifecycle || !parent || parent->processState().blocked))
        {
            throw std::logic_error("Use deferred scene changes during node callbacks");
        }
    }

    void SceneTree::enterBranch(Node& node)
    {
        if (node.processState().queuedForDeletion || node.processState().insideTree || m_StopRequested)
        {
            return;
        }
        node.processState().insideTree = true;
        node.onEnterTree();
        if (node.processState().behavior && !node.processState().queuedForDeletion && !m_StopRequested)
        {
            node.processState().behavior->enterTree();
        }
        node.processState().blocked = true;
        for (const auto& child : node.children())
        {
            enterBranch(*child);
        }
        node.processState().blocked = false;
    }

    void SceneTree::readyBranch(Node& node)
    {
        if (!node.processState().insideTree || node.processState().queuedForDeletion || m_StopRequested)
        {
            return;
        }
        node.processState().blocked = true;
        for (const auto& child : node.children())
        {
            readyBranch(*child);
        }
        node.processState().blocked = false;
        if (node.processState().queuedForDeletion || m_StopRequested)
        {
            return;
        }
        if (!node.processState().ready)
        {
            node.onReady();
            node.processState().ready = true;
        }
        if (node.processState().behavior && !node.processState().behaviorReady &&
            !node.processState().queuedForDeletion && !m_StopRequested)
        {
            node.processState().behavior->ready();
            node.processState().behaviorReady = true;
        }
    }

    void SceneTree::exitBranch(Node& node) noexcept
    {
        for (const auto& child : std::views::reverse(node.children()))
        {
            exitBranch(*child);
        }
        if (!node.processState().insideTree)
        {
            return;
        }
        // Keep identities and tree access valid until both exit callbacks have returned.
        try
        {
            if (node.processState().behavior)
            {
                node.processState().behavior->exitTree();
            }
        }
        catch (const std::exception& error)
        {
            Logger::app().error("Exit script on '{}': {}", node.name(), error.what());
        }
        try
        {
            node.onExitTree();
        }
        catch (const std::exception& error)
        {
            Logger::app().error("Exit node '{}': {}", node.name(), error.what());
        }
        node.processState().insideTree = false;
        node.processState().blocked    = false;
    }

    void SceneTree::start()
    {
        requireIdle();
        if (m_Running)
        {
            return;
        }
        m_Running     = true;
        m_Dispatching = true;
        m_Lifecycle   = true;
        try
        {
            enterBranch(root());
            readyBranch(root());
            for (const auto& [entity, node] : m_Registry->view<const SceneNode>().each())
            {
                if (!node.node)
                {
                    auto& process      = m_Registry->get<SceneProcess>(entity);
                    process.insideTree = true;
                    process.ready      = true;
                }
            }
            m_Lifecycle   = false;
            m_Dispatching = false;
            if (m_StopRequested)
            {
                stop();
            }
            else
            {
                flushDeferred();
            }
        }
        catch (...)
        {
            m_Dispatching = false;
            m_Lifecycle   = false;
            stop();
            throw;
        }
    }

    void SceneTree::stop() noexcept
    {
        if (m_Stopping)
        {
            return;
        }
        if (m_Dispatching || m_Flushing)
        {
            m_StopRequested = true;
            return;
        }
        if (m_Running)
        {
            m_Dispatching = true;
            m_Lifecycle   = false;
            m_Stopping    = true;
            exitBranch(root());
        }
        m_Dispatching   = false;
        m_Running       = false;
        m_StopRequested = false;
        m_Stopping      = false;
        m_Commands.clear();
        m_DeletionQueue.clear();
        m_ProcessOrder.clear();
        m_ProcessStructure = UINT64_MAX;
        m_Accumulator      = 0;
        if (!m_Registry)
        {
            return;
        }
        for (auto&& [entity, process] : m_Registry->view<SceneProcess>().each())
        {
            process.queuedForDeletion = false;
            process.insideTree        = false;
            process.blocked           = false;
        }
    }

    bool SceneTree::running() const
    {
        return m_Running;
    }

    bool SceneTree::dispatching() const
    {
        return m_Dispatching;
    }

    void SceneTree::dispatchProcess(double deltaSeconds, bool physics)
    {
        if (physics)
        {
            m_Input.beginPhysicsTick();
        }
        if (m_ProcessStructure != changes().structure || m_ProcessMetadata != changes().metadata)
        {
            m_ProcessOrder.clear();
            for (const auto handle : orderedEntities())
            {
                if (m_Registry->get<SceneNode>(handle).node)
                {
                    m_ProcessOrder.push_back({handle, m_ProcessOrder.size()});
                }
            }
            std::ranges::sort(m_ProcessOrder,
                              [&](const ProcessEntry& a, const ProcessEntry& b)
                              {
                                  const auto aPriority = m_Registry->get<SceneProcess>(a.entity).priority;
                                  const auto bPriority = m_Registry->get<SceneProcess>(b.entity).priority;
                                  return aPriority != bPriority ? aPriority < bPriority : a.treeOrder < b.treeOrder;
                              });
            m_ProcessStructure = changes().structure;
            m_ProcessMetadata  = changes().metadata;
        }
        m_Dispatching     = true;
        m_PhysicsDispatch = physics;
        try
        {
            for (const auto& entry : m_ProcessOrder)
            {
                if (m_StopRequested)
                {
                    break;
                }
                auto& node = *m_Registry->get<SceneNode>(entry.entity).node;
                if (!node.canProcess())
                {
                    continue;
                }
                if (physics)
                {
                    node.onPhysicsProcess(deltaSeconds);
                    if (node.processState().behavior && !node.isQueuedForDeletion() && !m_StopRequested)
                    {
                        node.processState().behavior->physicsProcess(deltaSeconds);
                    }
                }
                else
                {
                    node.onProcess(deltaSeconds);
                    if (node.processState().behavior && !node.isQueuedForDeletion() && !m_StopRequested)
                    {
                        node.processState().behavior->process(deltaSeconds);
                    }
                }
            }
            if (!m_StopRequested)
            {
                dispatchSystems(deltaSeconds, physics ? ScenePhase::ePhysics : ScenePhase::eProcess);
            }
            m_Dispatching     = false;
            m_PhysicsDispatch = false;
            if (m_StopRequested)
            {
                stop();
            }
            else
            {
                flushDeferred();
            }
        }
        catch (...)
        {
            m_Dispatching     = false;
            m_PhysicsDispatch = false;
            stop();
            throw;
        }
    }

    InputMap& SceneTree::input()
    {
        return m_Input;
    }

    ActionState SceneTree::action(std::string_view name) const
    {
        return m_PhysicsDispatch ? m_Input.physicsAction(name) : m_Input.action(name);
    }

    void SceneTree::advance(double deltaSeconds)
    {
        requireIdle();
        const auto scaled = deltaSeconds * m_TimeScale;
        if (!std::isfinite(deltaSeconds) || deltaSeconds < 0 || !std::isfinite(scaled) ||
            scaled / fixedDelta() >= double(std::numeric_limits<uint64_t>::max() - m_PhysicsFrame))
        {
            throw std::invalid_argument("Scene delta must be finite, nonnegative and fit the tick counter");
        }
        start();
        if (!m_Running)
        {
            return;
        }
        m_Accumulator += scaled;
        // Offline and interactive execution share the clock: requested ticks are never silently discarded.
        const auto ticks = static_cast<uint64_t>(std::floor(m_Accumulator / fixedDelta()));
        m_Accumulator -= double(ticks) * fixedDelta();
        for (uint64_t tick = 0; m_Running && tick < ticks; ++tick)
        {
            physicsStep();
        }
        if (m_Running)
        {
            dispatchProcess(scaled, false);
        }
    }

    void SceneTree::physicsStep()
    {
        requireIdle();
        if (m_PhysicsFrame == std::numeric_limits<uint64_t>::max())
        {
            throw std::overflow_error("Scene physics tick counter exhausted");
        }
        start();
        if (!m_Running)
        {
            return;
        }
        ++m_PhysicsFrame;
        dispatchProcess(fixedDelta(), true);
    }

    void SceneTree::stepPaused()
    {
        requireIdle();
        if (!m_Paused)
        {
            throw std::logic_error("Single-step requires a paused scene");
        }
        m_Paused = false;
        try
        {
            physicsStep();
        }
        catch (...)
        {
            m_Paused = true;
            throw;
        }
        m_Paused = true;
    }

    uint64_t SceneTree::physicsFrame() const
    {
        return m_PhysicsFrame;
    }

    double SceneTree::fixedDelta() const
    {
        return 1.0 / 60.0;
    }

    bool SceneTree::paused() const
    {
        return m_Paused;
    }

    void SceneTree::setPaused(bool paused)
    {
        m_Paused = paused;
    }

    double SceneTree::timeScale() const
    {
        return m_TimeScale;
    }

    void SceneTree::setTimeScale(double scale)
    {
        if (!std::isfinite(scale) || scale < 0)
        {
            throw std::invalid_argument("Scene time scale must be finite and nonnegative");
        }
        m_TimeScale = scale;
    }

    void SceneTree::attachBehavior(Node& node, NodeBehavior& behavior)
    {
        requireIdle();
        if (find(node.id()) != &node || node.processState().behavior)
        {
            throw std::invalid_argument("Attach one behavior to a node in this scene");
        }
        if (m_Running)
        {
            throw std::logic_error("Attach scripts before starting the scene");
        }
        node.processState().behavior      = &behavior;
        node.processState().behaviorReady = false;
    }

    void SceneTree::replaceBehavior(Node& node, NodeBehavior& previous, NodeBehavior& replacement)
    {
        requireIdle();
        if (find(node.id()) != &node || node.processState().behavior != &previous)
        {
            throw std::invalid_argument("Replacement must target the attached behavior");
        }
        node.processState().behavior = &replacement;
    }

    void SceneTree::detachBehavior(Node& node, NodeBehavior& behavior) noexcept
    {
        if (node.processState().behavior == &behavior)
        {
            node.processState().behavior      = nullptr;
            node.processState().behaviorReady = false;
        }
    }

    void SceneTree::queueFree(ObjectId id)
    {
        const auto handle = entity(id);
        if (handle == entt::null || handle == root().entity())
        {
            throw std::invalid_argument("Queue deletion requires a live non-root entity in this scene");
        }
        if (m_Registry->get<SceneProcess>(handle).queuedForDeletion)
        {
            return;
        }
        m_DeletionQueue.push_back(id);
        for (const auto candidate : branchEntities(handle))
        {
            m_Registry->get<SceneProcess>(candidate).queuedForDeletion = true;
        }
    }

    void SceneTree::deferAddChild(ObjectId parent, std::unique_ptr<Node> child)
    {
        if (!find(parent) || !child || child->parent())
        {
            throw std::invalid_argument("Deferred child requires a live parent and unique unattached ownership");
        }
        deferCommand(
            [parent, child = std::move(child)](SceneTree& scene) mutable
            {
                auto* target = scene.find(parent);
                if (!target || target->isQueuedForDeletion())
                {
                    throw std::invalid_argument("Deferred child targets a removed parent");
                }
                scene.addChild(*target, std::move(child));
            });
    }

    void SceneTree::deferReparent(ObjectId node, ObjectId parent, bool keepGlobalTransform)
    {
        if (!find(node) || !find(parent))
        {
            throw std::invalid_argument("Deferred reparent requires live nodes in this scene");
        }
        deferCommand(
            [node, parent, keepGlobalTransform](SceneTree& scene)
            {
                auto* target      = scene.find(node);
                auto* destination = scene.find(parent);
                if (!target || !destination || target->isQueuedForDeletion() || destination->isQueuedForDeletion())
                {
                    throw std::invalid_argument("Deferred reparent targets a removed node");
                }
                scene.reparent(*target, *destination, keepGlobalTransform);
            });
    }

    void SceneTree::flushDeferred()
    {
        requireIdle();
        m_Flushing = true;
        try
        {
            // Move the current queue: callbacks may enqueue work for the next boundary, never recurse here.
            auto commands = std::move(m_Commands);
            m_Commands.clear();
            for (auto& command : commands)
            {
                if (m_StopRequested)
                {
                    break;
                }
                command(*this);
            }
            auto deletions  = std::move(m_DeletionQueue);
            m_DeletionQueue = {};
            for (const auto id : deletions)
            {
                if (m_StopRequested)
                {
                    break;
                }
                if (entity(id) != entt::null)
                {
                    destroyEntity(id);
                }
            }
            m_Flushing = false;
            if (m_StopRequested)
            {
                stop();
            }
        }
        catch (...)
        {
            m_Flushing = false;
            stop();
            throw;
        }
    }
} // namespace vultra
