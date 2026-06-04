#pragma once

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>

namespace vultra
{
    // CPU-simulated particle emitter. Attached to an entity with a TransformComponent; the
    // ParticleSystem spawns and integrates particles from the entity's world position.
    //
    // (v1 simulates on the CPU and previews particles via the debug-draw path. A GPU compute
    // backend + instanced billboard rendering is the planned upgrade — the component data is
    // backend-agnostic.)
    struct ParticleEmitterComponent
    {
        bool playing {true};
        bool worldSpace {true}; // simulate in world space (true) or local to the emitter (false)

        uint32_t maxParticles {128};
        float    emissionRate {32.0f}; // particles spawned per second

        // Initial particle state (with per-particle random variance where noted).
        float     lifetime {2.0f};         // seconds
        float     lifetimeVariance {0.4f}; // +/- fraction of lifetime
        float     spawnRadius {0.1f};      // random spawn offset around the emitter
        glm::vec3 startVelocity {0.0f, 2.0f, 0.0f};
        float     velocityVariance {1.0f}; // random velocity magnitude added in a random direction
        glm::vec3 gravity {0.0f, -2.0f, 0.0f};

        // Appearance, interpolated over each particle's lifetime.
        float     startSize {0.12f};
        float     endSize {0.0f};
        glm::vec4 startColor {1.0f, 0.8f, 0.35f, 1.0f};
        glm::vec4 endColor {1.0f, 0.2f, 0.0f, 0.0f};
    };
} // namespace vultra
