#pragma once

#include "vultra/core/engine/engine_subsystem.hpp"

#include <entt/entity/entity.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <random>
#include <unordered_map>
#include <vector>

namespace vultra
{
    class IWorldService;
    class IRenderService;

    // CPU particle simulation driven by ParticleEmitterComponent. Particles are spawned from each
    // emitter entity's world position, integrated under gravity, and aged out. v1 previews them via
    // the debug-draw path; a GPU compute + billboard backend is the planned upgrade.
    class ParticleSystem final : public EngineSubsystem
    {
    public:
        ENGINE_SUBSYSTEM(ParticleSystem)

        bool onInit() override;
        void onShutdown() override;
        void onUpdate(fsec dt) override;

    private:
        struct Particle
        {
            glm::vec3 position {0.0f};
            glm::vec3 velocity {0.0f};
            float     age {0.0f};
            float     lifetime {1.0f};
        };

        struct EmitterState
        {
            std::vector<Particle> particles;
            float                 spawnAccumulator {0.0f};
        };

        IWorldService*                                 m_World {nullptr};
        IRenderService*                                m_Render {nullptr};
        std::unordered_map<entt::entity, EmitterState> m_States;
        std::mt19937                                   m_Rng {0xC0FFEEu};
    };
} // namespace vultra
