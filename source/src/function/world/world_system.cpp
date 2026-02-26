#include "vultra/function/world/world_system.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/core/engine/engine_context.hpp"
#include "vultra/function/world/components/hierarchy_component.hpp"
#include "vultra/function/world/components/transform_component.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace vultra
{
    bool WorldSystem::onInit()
    {
        VULTRA_CORE_INFO("[WorldSystem] Initializing...");

        VULTRA_CORE_TRACE("[WorldSystem] Creating world");
        m_World = std::make_unique<World>();

        VULTRA_CORE_TRACE("[WorldSystem] Providing IWorldService");
        ctx().services.provide<IWorldService>(this);

        VULTRA_CORE_INFO("[WorldSystem] Initialized!");

        return true;
    }

    void WorldSystem::onShutdown()
    {
        VULTRA_CORE_INFO("[WorldSystem] Shutting down");
        m_World.reset();
    }

    void WorldSystem::onPreRender() { updateWorldTransforms(); }

    static glm::mat4 makeLocalMatrix(const TransformComponent& t)
    {
        glm::mat4 m {1.0f};
        m = glm::translate(m, t.position);
        m *= glm::mat4_cast(t.rotation);
        m = glm::scale(m, t.scale);
        return m;
    }

    void WorldSystem::updateWorldTransforms()
    {
        if (!m_World)
            return;

        auto& w   = *m_World;
        auto& reg = w.registry();

        auto view = reg.view<TransformComponent, HierarchyComponent>();

        std::vector<entt::entity> roots;
        roots.reserve(view.size_hint());

        for (auto e : view)
        {
            const auto& h = view.get<HierarchyComponent>(e);
            if (h.parent == entt::null)
                roots.push_back(e);
        }

        struct StackItem
        {
            entt::entity e {entt::null};
            glm::mat4    parentWorld {1.0f};
            bool         parentDirty {false};
        };

        std::vector<StackItem> stack;
        stack.reserve(view.size_hint());

        for (auto root : roots)
            stack.push_back({root, glm::mat4(1.0f), false});

        while (!stack.empty())
        {
            auto item = stack.back();
            stack.pop_back();

            if (!reg.valid(item.e))
                continue;

            auto* t = reg.try_get<TransformComponent>(item.e);
            auto* h = reg.try_get<HierarchyComponent>(item.e);
            if (!t || !h)
                continue;

            const bool dirty = t->dirty || item.parentDirty;
            if (dirty)
            {
                t->worldMatrix = item.parentWorld * makeLocalMatrix(*t);
                t->dirty       = false;
            }

            glm::mat4 worldM = t->worldMatrix;

            // Push children
            for (auto child = h->firstChild; child != entt::null; child = w.nextSibling(child))
            {
                stack.push_back({child, worldM, dirty});
            }
        }
    }
} // namespace vultra
