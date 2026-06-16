#pragma once

#include "vultra/core/engine/engine_subsystem.hpp"
#include "vultra/function/services/navigation_service.hpp"

#include <entt/entity/entity.hpp>

#include <memory>
#include <vector>

namespace vultra
{
    class IWorldService;
    class IAssetService;
    class IPhysicsService;

    // Recast/Detour navigation. Bakes a navmesh from world geometry, answers path queries,
    // and steers NavAgentComponent entities (feeding the character controller when present).
    class NavigationSystem final : public EngineSubsystem, public INavigationService
    {
    public:
        ENGINE_SUBSYSTEM(NavigationSystem)

        NavigationSystem();
        ~NavigationSystem() override;

        bool onInit() override;
        void onShutdown() override;
        void onUpdate(fsec dt) override;

        bool                   bake() override;
        bool                   isBaked() const override;
        std::vector<glm::vec3> findPath(const glm::vec3& start, const glm::vec3& end) const override;
        glm::vec3              nearestPoint(const glm::vec3& point) const override;

        bool setAgentDestination(entt::entity entity, const glm::vec3& target) override;
        void stopAgent(entt::entity entity) override;
        bool agentHasPath(entt::entity entity) const override;

        void setDebugDrawEnabled(bool enabled) override;
        bool debugDrawEnabled() const override { return m_DebugDraw; }

    private:
        struct Impl;

        bool collectWorldGeometry(std::vector<float>& outVerts, std::vector<int>& outIndices) const;
        void steerAgents(float dt);
        void drawDebug() const;

        std::unique_ptr<Impl> m_Impl;

        IWorldService*   m_WorldService {nullptr};
        IAssetService*   m_AssetService {nullptr};
        IPhysicsService* m_PhysicsService {nullptr};

        bool m_DebugDraw {false};
    };
} // namespace vultra
