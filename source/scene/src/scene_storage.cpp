#include <vultra/scene/render_components.hpp>
#include <vultra/scene/scene_tree.hpp>

#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace vultra
{
    void SceneTree::markChanged(SceneChange change)
    {
        m_Changes->mark(change);
    }

    entt::registry& SceneTree::registry()
    {
        return *m_Registry;
    }

    entt::entity SceneTree::entity(NodeId id) const
    {
        const auto found = m_PersistentEntities.find(id.value.bytes);
        return found == m_PersistentEntities.end() ? entt::null : found->second;
    }

    ObjectId SceneTree::createEntity(std::string name)
    {
        requireStructuralBoundary();
        const auto id     = ObjectId::generate();
        const auto handle = m_Registry->create();
        try
        {
            const NodeId persistent {StableId::generate()};
            m_Registry->emplace<SceneNode>(handle, id, nullptr, persistent, std::move(name));
            m_Registry->emplace<SceneHierarchy>(handle);
            auto& process      = m_Registry->emplace<SceneProcess>(handle);
            process.insideTree = m_Running;
            process.ready      = m_Running;
            m_NodeEntities.emplace(id.value, handle);
            m_PersistentEntities.emplace(persistent.value.bytes, handle);
        }
        catch (...)
        {
            m_NodeEntities.erase(id.value);
            m_Registry->destroy(handle);
            throw;
        }
        markChanged(SceneChange::eStructure);
        return id;
    }

    void SceneTree::destroyEntity(ObjectId id)
    {
        const auto handle = entity(id);
        if (handle == entt::null)
        {
            throw std::invalid_argument("Destroy entity requires a live identity in this world");
        }
        if (m_Dispatching)
        {
            queueFree(id);
            return;
        }
        if (auto* node = m_Registry->get<SceneNode>(handle).node)
        {
            remove(*node);
            return;
        }
        retireRuntimeEntities(branchEntities(handle));
    }

    std::vector<entt::entity> SceneTree::branchEntities(entt::entity root) const
    {
        orderedEntities();
        std::vector<entt::entity> result;
        // Mark once in parent-first order rather than walking every entity's ancestry for deep subtrees.
        for (const auto handle : m_TransformOrder)
        {
            auto&      hierarchy = m_Registry->get<SceneHierarchy>(handle);
            const auto inBranch =
                handle == root || (hierarchy.parent != entt::null &&
                                   m_Registry->get<SceneHierarchy>(hierarchy.parent).traversalState == 3);
            hierarchy.traversalState = inBranch ? 3 : 2;
            if (inBranch)
            {
                result.push_back(handle);
            }
        }
        return result;
    }

    void SceneTree::retireRuntimeEntities(std::span<const entt::entity> entities)
    {
        // Callers select runtime-only entities; authoring facades transfer through remove/bindTree instead.
        for (const auto handle : std::views::reverse(entities))
        {
            const auto& metadata = m_Registry->get<SceneNode>(handle);
            m_PersistentEntities.erase(metadata.persistentId.value.bytes);
            m_NodeEntities.erase(metadata.id.value);
            m_Registry->destroy(handle);
        }
        if (m_CurrentCamera.value.valid() && entity(m_CurrentCamera) == entt::null)
        {
            m_CurrentCamera = {};
            markChanged(SceneChange::eCamera);
        }
        if (m_CurrentEnvironment.value.valid() && entity(m_CurrentEnvironment) == entt::null)
        {
            m_CurrentEnvironment = {};
            markChanged(SceneChange::eEnvironment);
        }
        markChanged(SceneChange::eStructure);
    }

    void SceneTree::setEntityParent(ObjectId id, ObjectId parentId, bool keepGlobalTransform)
    {
        requireStructuralBoundary();
        const auto handle = entity(id);
        const auto parent = entity(parentId);
        if (handle == entt::null || (parentId.value && parent == entt::null))
        {
            throw std::invalid_argument("Entity parent requires identities in this world");
        }
        if (m_Registry->get<SceneNode>(handle).node)
        {
            throw std::invalid_argument("Use reparent for authored Node ownership");
        }
        auto& hierarchy = m_Registry->get<SceneHierarchy>(handle);
        if (hierarchy.parent == parent)
        {
            return;
        }
        for (auto ancestor = parent; ancestor != entt::null;
             ancestor      = m_Registry->get<SceneHierarchy>(ancestor).parent)
        {
            if (ancestor == handle)
            {
                throw std::invalid_argument("Entity parent would create a hierarchy cycle");
            }
        }
        auto*     transform = m_Registry->try_get<SceneTransform>(handle);
        glm::mat4 local {1};
        if (transform && keepGlobalTransform)
        {
            syncTransforms();
            local = transform->global;
            if (const auto* parentTransform = m_Registry->try_get<SceneTransform>(parent))
            {
                const auto determinant = glm::determinant(parentTransform->global);
                if (!std::isfinite(determinant) || determinant == 0)
                {
                    throw std::invalid_argument("Cannot preserve entity transform under a singular parent");
                }
                local = glm::inverse(parentTransform->global) * local;
            }
            SceneTransform::validate(local);
        }
        hierarchy.parent = parent;
        if (transform && keepGlobalTransform)
        {
            transform->local = local;
        }
        markChanged(SceneChange::eStructure);
    }

    std::span<const entt::entity> SceneTree::orderedEntities() const
    {
        if (m_OrderRevision == changes().structure)
        {
            return m_EntityOrder;
        }
        m_EntityOrder.clear();
        std::vector<entt::entity> pending {root().entity()};
        while (!pending.empty())
        {
            const auto handle = pending.back();
            pending.pop_back();
            m_EntityOrder.push_back(handle);
            for (const auto child : std::views::reverse(m_Registry->get<SceneHierarchy>(handle).children))
            {
                pending.push_back(child);
            }
        }
        const auto authored = m_EntityOrder.size();
        for (const auto& [handle, metadata] : m_Registry->view<SceneNode>().each())
        {
            if (!metadata.node)
            {
                m_EntityOrder.push_back(handle);
            }
            m_Registry->get<SceneHierarchy>(handle).traversalState = 0;
        }
        std::sort(m_EntityOrder.begin() + authored,
                  m_EntityOrder.end(),
                  [&](const auto a, const auto b)
                  {
                      return m_Registry->get<SceneNode>(a).id.value < m_Registry->get<SceneNode>(b).id.value;
                  });
        m_TransformOrder.clear();
        // Iterative parent traversal avoids recursion for large/deep simulation hierarchies.
        for (size_t index = 0; index < m_EntityOrder.size(); ++index)
        {
            auto handle                                  = m_EntityOrder[index];
            m_Registry->get<SceneNode>(handle).treeOrder = index;
            while (handle != entt::null && m_Registry->get<SceneHierarchy>(handle).traversalState != 2)
            {
                auto& hierarchy = m_Registry->get<SceneHierarchy>(handle);
                if (hierarchy.traversalState == 1)
                {
                    throw std::invalid_argument("ECS hierarchy contains a cycle");
                }
                if (hierarchy.parent != entt::null &&
                    (!m_Registry->valid(hierarchy.parent) || !m_Registry->all_of<SceneHierarchy>(hierarchy.parent)))
                {
                    throw std::invalid_argument("ECS hierarchy has a removed parent");
                }
                hierarchy.traversalState = 1;
                pending.push_back(handle);
                handle = hierarchy.parent;
            }
            while (!pending.empty())
            {
                handle = pending.back();
                pending.pop_back();
                m_Registry->get<SceneHierarchy>(handle).traversalState = 2;
                m_TransformOrder.push_back(handle);
            }
        }
        m_OrderRevision = changes().structure;
        return m_EntityOrder;
    }

    void SceneTree::syncTransforms() const
    {
        if (m_TransformRevision == changes().transforms && m_TransformStructure == changes().structure)
        {
            return;
        }
        orderedEntities();
        for (const auto handle : m_TransformOrder)
        {
            if (auto* transform = m_Registry->try_get<SceneTransform>(handle))
            {
                SceneTransform::validate(transform->local);
                const auto  parent          = m_Registry->get<SceneHierarchy>(handle).parent;
                const auto* parentTransform = m_Registry->try_get<SceneTransform>(parent);
                transform->global = parentTransform ? parentTransform->global * transform->local : transform->local;
            }
        }
        m_TransformRevision  = changes().transforms;
        m_TransformStructure = changes().structure;
    }

    bool SceneTree::canProcess(entt::entity handle) const
    {
        const auto& process = m_Registry->get<SceneProcess>(handle);
        if (!process.insideTree || process.queuedForDeletion)
        {
            return false;
        }
        auto mode = process.mode;
        for (auto parent = m_Registry->get<SceneHierarchy>(handle).parent;
             mode == ProcessMode::eInherit && parent != entt::null;
             parent = m_Registry->get<SceneHierarchy>(parent).parent)
        {
            mode = m_Registry->get<SceneProcess>(parent).mode;
        }
        switch (mode)
        {
            case ProcessMode::eInherit:
            case ProcessMode::ePausable:
                return !paused();
            case ProcessMode::eWhenPaused:
                return paused();
            case ProcessMode::eAlways:
                return true;
            case ProcessMode::eDisabled:
                return false;
        }
        throw std::invalid_argument("Entity has an invalid process mode");
    }

    SceneSystemId SceneTree::addSystem(ScenePhase phase, System system)
    {
        requireIdle();
        if (!system || phase < ScenePhase::ePhysics || phase > ScenePhase::eProcess || m_NextSystemId == UINT64_MAX)
        {
            throw std::invalid_argument("Scene system requires a valid phase and callback");
        }
        const auto id = m_NextSystemId++;
        m_Systems[size_t(phase)].push_back({id, std::move(system)});
        return {root().id(), id};
    }

    void SceneTree::removeSystem(SceneSystemId id)
    {
        requireIdle();
        if (id.world != root().id())
        {
            throw std::invalid_argument("Scene system belongs to a different world");
        }
        for (auto& phase : m_Systems)
        {
            const auto found = std::ranges::find(phase, id.value, &SystemEntry::id);
            if (found != phase.end())
            {
                phase.erase(found);
                return;
            }
        }
        throw std::invalid_argument("Scene system does not belong to this world");
    }

    void SceneTree::dispatchSystems(double deltaSeconds, ScenePhase phase)
    {
        for (auto& system : m_Systems[size_t(phase)])
        {
            if (m_StopRequested)
            {
                break;
            }
            system.callback(*this, deltaSeconds);
        }
    }

    void SceneTree::deferCommand(std::move_only_function<void(SceneTree&)> command)
    {
        if (!command)
        {
            throw std::invalid_argument("Deferred ECS command is empty");
        }
        m_Commands.push_back(std::move(command));
    }

    void SceneTree::transformChanged(entt::registry&, entt::entity)
    {
        markChanged(SceneChange::eTransform);
    }

    void SceneTree::transformRemoved(entt::registry& registry, entt::entity entity)
    {
        // Identity lookup remains safe when entity retirement destroys component pools in an unspecified order.
        if (this->entity(m_CurrentCamera) == entity)
        {
            m_CurrentCamera = {};
            markChanged(SceneChange::eCamera);
        }
        transformChanged(registry, entity);
    }

    void SceneTree::hierarchyChanged(entt::registry&, entt::entity)
    {
        markChanged(SceneChange::eStructure);
    }

    void SceneTree::meshChanged(entt::registry&, entt::entity)
    {
        markChanged(SceneChange::eStructure);
        markChanged(SceneChange::eMaterial);
    }

    void SceneTree::cameraChanged(entt::registry&, entt::entity)
    {
        markChanged(SceneChange::eCamera);
    }

    void SceneTree::cameraRemoved(entt::registry& registry, entt::entity entity)
    {
        if (this->entity(m_CurrentCamera) == entity)
        {
            m_CurrentCamera = {};
        }
        cameraChanged(registry, entity);
    }

    void SceneTree::lightChanged(entt::registry&, entt::entity)
    {
        m_SceneLighting = true;
        markChanged(SceneChange::eLighting);
    }

    void SceneTree::environmentChanged(entt::registry&, entt::entity)
    {
        markChanged(SceneChange::eEnvironment);
    }

    void SceneTree::environmentRemoved(entt::registry& registry, entt::entity entity)
    {
        if (this->entity(m_CurrentEnvironment) == entity)
        {
            m_CurrentEnvironment = {};
        }
        environmentChanged(registry, entity);
    }

    void SceneTree::processChanged(entt::registry&, entt::entity)
    {
        markChanged(SceneChange::eMetadata);
    }

    void SceneTree::connectComponentSignals()
    {
        m_Registry->on_construct<SceneTransform>().connect<&SceneTree::transformChanged>(*this);
        m_Registry->on_update<SceneTransform>().connect<&SceneTree::transformChanged>(*this);
        m_Registry->on_destroy<SceneTransform>().connect<&SceneTree::transformRemoved>(*this);
        m_Registry->on_update<SceneHierarchy>().connect<&SceneTree::hierarchyChanged>(*this);
        m_Registry->on_construct<SceneMesh>().connect<&SceneTree::meshChanged>(*this);
        m_Registry->on_update<SceneMesh>().connect<&SceneTree::meshChanged>(*this);
        m_Registry->on_destroy<SceneMesh>().connect<&SceneTree::meshChanged>(*this);
        m_Registry->on_construct<SceneCamera>().connect<&SceneTree::cameraChanged>(*this);
        m_Registry->on_update<SceneCamera>().connect<&SceneTree::cameraChanged>(*this);
        m_Registry->on_destroy<SceneCamera>().connect<&SceneTree::cameraRemoved>(*this);
        m_Registry->on_construct<SceneLight>().connect<&SceneTree::lightChanged>(*this);
        m_Registry->on_update<SceneLight>().connect<&SceneTree::lightChanged>(*this);
        m_Registry->on_destroy<SceneLight>().connect<&SceneTree::lightChanged>(*this);
        m_Registry->on_construct<SceneEnvironment>().connect<&SceneTree::environmentChanged>(*this);
        m_Registry->on_update<SceneEnvironment>().connect<&SceneTree::environmentChanged>(*this);
        m_Registry->on_destroy<SceneEnvironment>().connect<&SceneTree::environmentRemoved>(*this);
        m_Registry->on_update<SceneProcess>().connect<&SceneTree::processChanged>(*this);
    }

    void SceneTree::disconnectComponentSignals()
    {
        if (!m_Registry)
        {
            return;
        }
        const auto disconnect = [this]<class T>()
        {
            m_Registry->on_construct<T>().disconnect(this);
            m_Registry->on_update<T>().disconnect(this);
            m_Registry->on_destroy<T>().disconnect(this);
        };
        disconnect.template operator()<SceneTransform>();
        disconnect.template operator()<SceneHierarchy>();
        disconnect.template operator()<SceneMesh>();
        disconnect.template operator()<SceneCamera>();
        disconnect.template operator()<SceneLight>();
        disconnect.template operator()<SceneEnvironment>();
        disconnect.template operator()<SceneProcess>();
    }
} // namespace vultra
