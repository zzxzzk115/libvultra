#pragma once

#include "vultra/function/world/components/hierarchy_component.hpp"

#include <entt/entt.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace vultra
{
    class World
    {
    public:
        World();
        ~World();

        World(const World&)            = delete;
        World& operator=(const World&) = delete;

        entt::registry&       registry() { return m_Registry; }
        const entt::registry& registry() const { return m_Registry; }

        void clear();

        // Debug identity used by tooling (e.g. the editor World Viewer).
        void                     setDebugName(std::string name) { m_DebugName = std::move(name); }
        [[nodiscard]] const std::string& debugName() const { return m_DebugName; }
        [[nodiscard]] std::uint64_t      instanceId() const { return m_InstanceId; }

        // Live-instance registry: lets tooling enumerate every World currently alive in the
        // process, to spot worlds that were not released in time. Thread-safe; worlds may be
        // created on async scene-load threads.
        static void        forEachLive(const std::function<void(World&)>& fn);
        static std::size_t liveCount();

        // Basic entity ops
        entt::entity createEntity();
        void         destroyEntity(entt::entity e);

        // Hierarchy ops (World owns tree invariants)
        void         setParent(entt::entity child, entt::entity parent);
        void         insertBefore(entt::entity child, entt::entity sibling);
        void         insertAfter(entt::entity child, entt::entity sibling);
        void         removeParent(entt::entity child);
        entt::entity createChild(entt::entity parent);
        void         destroyRecursive(entt::entity root);

        // Iteration helpers
        entt::entity firstChild(entt::entity e) const;
        entt::entity nextSibling(entt::entity e) const;
        entt::entity parent(entt::entity e) const;

    private:
        entt::registry m_Registry;
        entt::entity   m_FirstRoot {entt::null};

        std::string   m_DebugName;
        std::uint64_t m_InstanceId {0};

        HierarchyComponent&       ensureHierarchy(entt::entity e);
        const HierarchyComponent* tryHierarchy(entt::entity e) const;

        void detachFromParent(entt::entity child);
        void attachToParent(entt::entity child, entt::entity parent, entt::entity beforeSibling = entt::null);
        bool isDescendantOf(entt::entity entity, entt::entity possibleAncestor) const;
    };
} // namespace vultra
