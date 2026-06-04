#include "vultra/function/particle/particle_system.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/function/services/render_service.hpp"
#include "vultra/function/services/world_service.hpp"
#include "vultra/function/world/components/particle_emitter_component.hpp"
#include "vultra/function/world/components/transform_component.hpp"
#include "vultra/function/world/world.hpp"

#include <glm/common.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

namespace vultra
{
    namespace
    {
        float randf(std::mt19937& rng, float lo, float hi)
        {
            std::uniform_real_distribution<float> dist(lo, hi);
            return dist(rng);
        }

        glm::vec3 randUnitVec(std::mt19937& rng)
        {
            const float z = randf(rng, -1.0f, 1.0f);
            const float a = randf(rng, 0.0f, 6.2831853f);
            const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
            return {r * std::cos(a), r * std::sin(a), z};
        }
    } // namespace

    bool ParticleSystem::onInit()
    {
        VULTRA_CORE_INFO("[ParticleSystem] Initialized!");
        return true;
    }

    void ParticleSystem::onShutdown()
    {
        m_States.clear();
        m_World  = nullptr;
        m_Render = nullptr;
    }

    void ParticleSystem::onUpdate(const fsec dtSec)
    {
        // Resolve services lazily so this system can update before RenderSystem (queuing debug
        // draws for the same frame) while still picking up the render service once it exists.
        if (m_World == nullptr)
            m_World = ctx().services.tryGet<IWorldService>();
        if (m_Render == nullptr)
            m_Render = ctx().services.tryGet<IRenderService>();
        if (m_World == nullptr)
            return;
        const float dt = dtSec.count();
        if (dt <= 0.0f)
            return;

        auto& reg = m_World->world().registry();

        // Drop simulation state for emitters that no longer exist.
        for (auto it = m_States.begin(); it != m_States.end();)
        {
            if (!reg.valid(it->first) || !reg.any_of<ParticleEmitterComponent>(it->first))
                it = m_States.erase(it);
            else
                ++it;
        }

        const auto view = reg.view<ParticleEmitterComponent, TransformComponent>();
        for (const auto e : view)
        {
            const auto& emitter   = view.get<ParticleEmitterComponent>(e);
            const auto& transform = view.get<TransformComponent>(e);
            auto&       state     = m_States[e];

            const glm::vec3 origin = glm::vec3(transform.worldMatrix[3]);

            // Spawn new particles at the configured rate (capped to maxParticles).
            if (emitter.playing && emitter.emissionRate > 0.0f)
            {
                state.spawnAccumulator += emitter.emissionRate * dt;
                while (state.spawnAccumulator >= 1.0f)
                {
                    state.spawnAccumulator -= 1.0f;
                    if (state.particles.size() >= emitter.maxParticles)
                        break;

                    Particle p;
                    p.position = origin + randUnitVec(m_Rng) * randf(m_Rng, 0.0f, emitter.spawnRadius);
                    p.velocity = emitter.startVelocity + randUnitVec(m_Rng) * randf(m_Rng, 0.0f, emitter.velocityVariance);
                    p.age      = 0.0f;
                    p.lifetime = glm::max(0.05f, emitter.lifetime * (1.0f + randf(m_Rng, -emitter.lifetimeVariance,
                                                                                 emitter.lifetimeVariance)));
                    state.particles.push_back(p);
                }
            }

            // Integrate and age, removing dead particles (swap-and-pop).
            for (std::size_t i = 0; i < state.particles.size();)
            {
                Particle& p = state.particles[i];
                p.velocity += emitter.gravity * dt;
                p.position += p.velocity * dt;
                p.age += dt;
                if (p.age >= p.lifetime)
                {
                    state.particles[i] = state.particles.back();
                    state.particles.pop_back();
                    continue;
                }
                ++i;
            }

            // Preview render: a small box per particle, size/colour interpolated over its lifetime.
            if (m_Render != nullptr)
            {
                for (const Particle& p : state.particles)
                {
                    const float t     = glm::clamp(p.age / p.lifetime, 0.0f, 1.0f);
                    const float size  = glm::mix(emitter.startSize, emitter.endSize, t);
                    const auto  color = glm::mix(emitter.startColor, emitter.endColor, t);
                    if (size <= 0.0f)
                        continue;
                    const glm::mat4 world = glm::translate(glm::mat4 {1.0f}, p.position);
                    m_Render->debugDrawBox(world, glm::vec3 {size * 0.5f}, glm::vec3 {color});
                }
            }
        }
    }
} // namespace vultra
