#include "vultra/function/world/world.hpp"

#include "vultra/core/base/uuid.hpp"
#include "vultra/function/world/components/hierarchy_component.hpp"
#include "vultra/function/world/components/id_component.hpp"
#include "vultra/function/world/components/transform_component.hpp"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <ranges>
#include <vector>

namespace vultra
{
    namespace
    {
        // Function-local statics (Meyers singletons) avoid static init-order fiasco: the registry
        // is constructed on first use, which is guaranteed to be before the first World is created.
        std::mutex& liveWorldsMutex()
        {
            static std::mutex m;
            return m;
        }

        std::vector<World*>& liveWorlds()
        {
            static std::vector<World*> worlds;
            return worlds;
        }

        std::atomic<std::uint64_t>& worldInstanceCounter()
        {
            static std::atomic<std::uint64_t> counter {0};
            return counter;
        }
    } // namespace

    World::World()
    {
        m_InstanceId = worldInstanceCounter().fetch_add(1, std::memory_order_relaxed) + 1;
        std::lock_guard lock {liveWorldsMutex()};
        liveWorlds().push_back(this);
    }

    World::~World()
    {
        std::lock_guard lock {liveWorldsMutex()};
        auto& worlds = liveWorlds();
        if (const auto it = std::find(worlds.begin(), worlds.end(), this); it != worlds.end())
            worlds.erase(it);
    }

    void World::forEachLive(const std::function<void(World&)>& fn)
    {
        if (!fn)
            return;

        // Snapshot under lock, then invoke outside it so the callback may safely call back into
        // World (and so a World destroyed during iteration can't dangle the locked vector).
        std::vector<World*> snapshot;
        {
            std::lock_guard lock {liveWorldsMutex()};
            snapshot = liveWorlds();
        }
        for (World* world : snapshot)
            fn(*world);
    }

    std::size_t World::liveCount()
    {
        std::lock_guard lock {liveWorldsMutex()};
        return liveWorlds().size();
    }

    void World::clear()
    {
        m_Registry.clear();
        m_FirstRoot = entt::null;
    }

    std::vector<entt::entity> World::roots() const
    {
        std::vector<entt::entity> out;
        for (entt::entity e = m_FirstRoot; e != entt::null; e = nextSibling(e))
            out.push_back(e);
        return out;
    }

    entt::entity World::createEntity()
    {
        entt::entity e = m_Registry.create();
        // Ensure stable identity + hierarchy links for scene/world workflows.
        m_Registry.emplace<IDComponent>(e, IDComponent {CoreUUIDHelper::createStandardUUID()});
        m_Registry.emplace<HierarchyComponent>(e);
        m_Registry.emplace<TransformComponent>(e);
        attachToParent(e, entt::null);
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
        if (hc.parent != entt::null)
        {
            auto& parentH = ensureHierarchy(hc.parent);
            if (parentH.firstChild == child)
                parentH.firstChild = hc.nextSibling;
            if (parentH.childCount > 0)
                parentH.childCount--;
        }
        else if (m_FirstRoot == child)
        {
            m_FirstRoot = hc.nextSibling;
        }

        if (hc.prevSibling != entt::null)
            ensureHierarchy(hc.prevSibling).nextSibling = hc.nextSibling;
        if (hc.nextSibling != entt::null)
            ensureHierarchy(hc.nextSibling).prevSibling = hc.prevSibling;

        hc.parent      = entt::null;
        hc.prevSibling = entt::null;
        hc.nextSibling = entt::null;
    }

    void World::attachToParent(entt::entity child, entt::entity parent, entt::entity beforeSibling)
    {
        auto& childH  = ensureHierarchy(child);
        auto* parentH = parent != entt::null ? &ensureHierarchy(parent) : nullptr;

        childH.parent = parent;
        if (parentH)
            parentH->childCount++;

        if (beforeSibling == child ||
            (beforeSibling != entt::null &&
             (!m_Registry.valid(beforeSibling) || ensureHierarchy(beforeSibling).parent != parent)))
            beforeSibling = entt::null;

        if (beforeSibling != entt::null)
        {
            auto& beforeH = ensureHierarchy(beforeSibling);

            childH.prevSibling  = beforeH.prevSibling;
            childH.nextSibling  = beforeSibling;
            beforeH.prevSibling = child;

            if (childH.prevSibling != entt::null)
                ensureHierarchy(childH.prevSibling).nextSibling = child;
            else if (parentH)
                parentH->firstChild = child;
            else
                m_FirstRoot = child;

            return;
        }

        // Append by default so load/save and editor operations preserve visible sibling order.
        entt::entity last  = entt::null;
        entt::entity first = parentH ? parentH->firstChild : m_FirstRoot;
        for (entt::entity c = first; c != entt::null; c = ensureHierarchy(c).nextSibling)
            last = c;

        childH.prevSibling = last;
        childH.nextSibling = entt::null;

        if (last != entt::null)
            ensureHierarchy(last).nextSibling = child;
        else if (parentH)
            parentH->firstChild = child;
        else
            m_FirstRoot = child;
    }

    bool World::isDescendantOf(entt::entity entity, entt::entity possibleAncestor) const
    {
        for (auto p = parent(entity); p != entt::null; p = parent(p))
        {
            if (p == possibleAncestor)
                return true;
        }
        return false;
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

        if (parent != entt::null && isDescendantOf(parent, child))
            parent = entt::null;

        detachFromParent(child);
        attachToParent(child, parent);
    }

    void World::insertBefore(entt::entity child, entt::entity sibling)
    {
        if (!m_Registry.valid(child) || !m_Registry.valid(sibling) || child == sibling)
            return;

        const entt::entity parentEntity = parent(sibling);
        if (parentEntity == child || (parentEntity != entt::null && isDescendantOf(parentEntity, child)))
            return;

        detachFromParent(child);
        attachToParent(child, parentEntity, sibling);
    }

    void World::insertAfter(entt::entity child, entt::entity sibling)
    {
        if (!m_Registry.valid(child) || !m_Registry.valid(sibling) || child == sibling)
            return;

        const entt::entity parentEntity = parent(sibling);
        if (parentEntity == child || (parentEntity != entt::null && isDescendantOf(parentEntity, child)))
            return;

        const entt::entity before = nextSibling(sibling);
        if (before == child)
            return;
        detachFromParent(child);
        attachToParent(child, parentEntity, before);
    }

    void World::removeParent(entt::entity child)
    {
        detachFromParent(child);
        if (m_Registry.valid(child))
            attachToParent(child, entt::null);
    }

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
        if (e == entt::null)
            return m_FirstRoot;
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
