#pragma once

#include "vultra/function/world/components/hierarchy_component.hpp"

#include <entt/entt.hpp>

namespace vultra
{
    class World
    {
    public:
        World()  = default;
        ~World() = default;

        entt::registry&       registry() { return m_Registry; }
        const entt::registry& registry() const { return m_Registry; }

        void clear();

        // Basic entity ops
        entt::entity createEntity();
        void         destroyEntity(entt::entity e);

        // Hierarchy ops (World owns tree invariants)
        void         setParent(entt::entity child, entt::entity parent);
        void         removeParent(entt::entity child);
        entt::entity createChild(entt::entity parent);
        void         destroyRecursive(entt::entity root);

        // Iteration helpers
        entt::entity firstChild(entt::entity e) const;
        entt::entity nextSibling(entt::entity e) const;
        entt::entity parent(entt::entity e) const;

    private:
        entt::registry m_Registry;

        HierarchyComponent&       ensureHierarchy(entt::entity e);
        const HierarchyComponent* tryHierarchy(entt::entity e) const;

        void detachFromParent(entt::entity child);
        void attachToParent(entt::entity child, entt::entity parent);
    };
} // namespace vultra
