#include "vultra/function/world/world.hpp"

#include "vultra/core/base/uuid.hpp"
#include "vultra/function/world/components/hierarchy_component.hpp"
#include "vultra/function/world/components/id_component.hpp"
#include "vultra/function/world/components/transform_component.hpp"

#include <ranges>
#include <vector>

namespace vultra
{
    void World::clear() { m_Registry.clear(); }

    entt::entity World::createEntity()
    {
        entt::entity e = m_Registry.create();
        // Ensure stable identity + hierarchy links for scene/world workflows.
        m_Registry.emplace<IDComponent>(e, IDComponent {CoreUUIDHelper::createStandardUUID()});
        m_Registry.emplace<HierarchyComponent>(e);
        m_Registry.emplace<TransformComponent>(e);
        return e;
    }

    void World::destroyEntity(entt::entity e)
    {
        if (m_Registry.valid(e))
        {
            // Maintain hierarchy invariants.
            if (const auto* h = tryHierarchy(e))
            {
                // If this entity has children, caller should use destroyRecursive.
                // Still detach from parent to avoid dangling sibling links.
                (void)h;
                detachFromParent(e);
            }
            m_Registry.destroy(e);
        }
    }

    HierarchyComponent& World::ensureHierarchy(entt::entity e)
    {
        if (!m_Registry.all_of<HierarchyComponent>(e))
        {
            return m_Registry.emplace<HierarchyComponent>(e);
        }
        return m_Registry.get<HierarchyComponent>(e);
    }

    const HierarchyComponent* World::tryHierarchy(entt::entity e) const
    {
        return m_Registry.all_of<HierarchyComponent>(e) ? &m_Registry.get<HierarchyComponent>(e) : nullptr;
    }

    void World::detachFromParent(entt::entity child)
    {
        if (!m_Registry.valid(child) || !m_Registry.all_of<HierarchyComponent>(child))
            return;

        auto& hc = m_Registry.get<HierarchyComponent>(child);
        if (hc.parent == entt::null)
            return;

        auto& parentH = ensureHierarchy(hc.parent);

        // Remove from parent's child list.
        if (parentH.firstChild == child)
            parentH.firstChild = hc.nextSibling;

        if (hc.prevSibling != entt::null)
            ensureHierarchy(hc.prevSibling).nextSibling = hc.nextSibling;
        if (hc.nextSibling != entt::null)
            ensureHierarchy(hc.nextSibling).prevSibling = hc.prevSibling;

        if (parentH.childCount > 0)
            parentH.childCount--;

        hc.parent      = entt::null;
        hc.prevSibling = entt::null;
        hc.nextSibling = entt::null;
    }

    void World::attachToParent(entt::entity child, entt::entity parent)
    {
        auto& childH  = ensureHierarchy(child);
        auto& parentH = ensureHierarchy(parent);

        childH.parent = parent;

        // Insert at head (cheap + stable). Editor can later support ordering.
        entt::entity oldFirst = parentH.firstChild;
        parentH.firstChild    = child;
        parentH.childCount++;

        childH.prevSibling = entt::null;
        childH.nextSibling = oldFirst;

        if (oldFirst != entt::null)
            ensureHierarchy(oldFirst).prevSibling = child;
    }

    void World::setParent(entt::entity child, entt::entity parent)
    {
        if (!m_Registry.valid(child))
            return;

        if (parent != entt::null && !m_Registry.valid(parent))
            parent = entt::null;

        // Prevent self-parent.
        if (child == parent)
            parent = entt::null;

        detachFromParent(child);

        if (parent != entt::null)
            attachToParent(child, parent);
    }

    void World::removeParent(entt::entity child) { detachFromParent(child); }

    entt::entity World::createChild(entt::entity parent)
    {
        entt::entity e = createEntity();
        if (parent != entt::null)
            setParent(e, parent);
        return e;
    }

    void World::destroyRecursive(entt::entity root)
    {
        if (!m_Registry.valid(root))
            return;

        // Gather subtree in a stable order (post-order) to avoid invalidating sibling links mid-iteration.
        std::vector<entt::entity> stack;
        std::vector<entt::entity> post;
        stack.push_back(root);

        while (!stack.empty())
        {
            entt::entity e = stack.back();
            stack.pop_back();
            post.push_back(e);

            const auto* h = tryHierarchy(e);
            for (entt::entity c = h ? h->firstChild : entt::null; c != entt::null;
                 c              = tryHierarchy(c) ? tryHierarchy(c)->nextSibling : entt::null)
            {
                if (m_Registry.valid(c))
                    stack.push_back(c);
            }
        }

        // Post-order destroy (children first)
        for (auto e : std::ranges::reverse_view(post))
        {
            if (!m_Registry.valid(e))
                continue;

            detachFromParent(e);
            m_Registry.destroy(e);
        }
    }

    entt::entity World::firstChild(entt::entity e) const
    {
        if (const auto* h = tryHierarchy(e))
            return h->firstChild;
        return entt::null;
    }

    entt::entity World::nextSibling(entt::entity e) const
    {
        if (const auto* h = tryHierarchy(e))
            return h->nextSibling;
        return entt::null;
    }

    entt::entity World::parent(entt::entity e) const
    {
        if (const auto* h = tryHierarchy(e))
            return h->parent;
        return entt::null;
    }
} // namespace vultra
