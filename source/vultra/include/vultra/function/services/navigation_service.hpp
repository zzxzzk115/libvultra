#pragma once

#include <vbase/service/service_registry.hpp>

#include <entt/entity/entity.hpp>
#include <entt/entity/fwd.hpp>
#include <glm/vec3.hpp>

#include <vector>

namespace vultra
{
    // Recast/Detour navigation: bake a navmesh from level geometry, query paths, and steer
    // NavAgentComponent entities. Exposed to Lua as the `Nav` namespace.
    class INavigationService
    {
    public:
        SERVICE_REGISTER(INavigationService)

        virtual ~INavigationService() = default;

        // Bake a navmesh from the current world's static mesh geometry (entities with a
        // MeshComponent + TransformComponent and an imported mesh). Returns false if there is
        // no geometry or the build fails.
        virtual bool bake() = 0;
        virtual bool isBaked() const = 0;

        // Find a path from `start` to `end` across the baked navmesh. Returns an ordered list
        // of waypoints (empty if unreachable or not baked). Points are snapped to the navmesh.
        virtual std::vector<glm::vec3> findPath(const glm::vec3& start, const glm::vec3& end) const = 0;

        // Closest point on the navmesh to `point` (returns `point` unchanged if not baked).
        virtual glm::vec3 nearestPoint(const glm::vec3& point) const = 0;

        // --- Agents (NavAgentComponent) ---
        virtual bool setAgentDestination(entt::entity entity, const glm::vec3& target) = 0;
        virtual void stopAgent(entt::entity entity) = 0;
        virtual bool agentHasPath(entt::entity entity) const = 0;

        // Editor/debug: draw the navmesh + active agent paths via debug-draw.
        virtual void setDebugDrawEnabled(bool enabled) = 0;
        virtual bool debugDrawEnabled() const = 0;
    };
} // namespace vultra
