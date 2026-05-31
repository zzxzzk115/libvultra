#pragma once

#include "vultra/core/engine/engine_subsystem.hpp"
#include "vultra/function/services/physics_service.hpp"

#include <entt/entt.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <memory>

namespace JPH
{
    class BodyID;
    class BodyInterface;
    class JobSystem;
    class PhysicsSystem;
    class TempAllocator;
}

namespace vultra
{
    class ITimingService;
    class IJobService;
    class IWorldService;

    class PhysicsSystem final : public EngineSubsystem, public IPhysicsService
    {
    public:
        ENGINE_SUBSYSTEM(PhysicsSystem)

        PhysicsSystem();
        ~PhysicsSystem() override;

        bool onInit() override;
        void onShutdown() override;
        void onPhysics(fsec dt) override;

        void setEnabled(bool enabled) override;
        bool enabled() const override { return m_Enabled; }

        void setPlaybackState(bool playing, bool paused) override;
        bool playing() const override { return m_Playing; }
        bool paused() const override { return m_Paused; }
        void requestSingleStep() override;

        void setFixedTimeStep(float seconds) override;
        float fixedTimeStep() const override { return m_FixedTimeStep; }

        uint32_t bodyCount() const override;

    private:
        struct Impl;
        struct BodyRecord;
        struct BodySignature;

        void ensureJoltGlobals();
        void releaseJoltGlobals();
        void syncWorldBodies();
        void stepSimulation(float seconds);
        void syncDynamicBodiesToWorld();
        void removeStaleBodies();
        void clearBodies();

        bool ensureBody(entt::entity entity);
        void destroyBody(entt::entity entity);
        bool buildSignature(entt::entity entity, BodySignature& out) const;

        std::unique_ptr<Impl> m_Impl;

        IWorldService* m_WorldService {nullptr};
        IJobService*   m_JobService {nullptr};
        ITimingService* m_TimingService {nullptr};

        bool  m_Enabled {true};
        bool  m_Playing {true};
        bool  m_Paused {false};
        bool  m_JoltGlobalsAcquired {false};
        float m_FixedTimeStep {1.0f / 60.0f};
        float m_Accumulator {0.0f};
        uint32_t m_FallbackMaxSubSteps {8};
        uint32_t m_PendingSingleSteps {0};

    };
} // namespace vultra
