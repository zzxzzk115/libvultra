#pragma once

#include "vultra/core/base/script_annotations.hpp"

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>

namespace vultra
{
    // Particle emitter. Attached to an entity with a TransformComponent; particles spawn from the
    // entity's world position.
    //
    // Two backends share this authoring data:
    //  - GPU (default, gpu == true): the simulation runs in a compute shader and particles render as
    //    instanced, camera-facing, additive billboards via builtin render-graph passes.
    //  - CPU (gpu == false): the ParticleSystem subsystem simulates on the CPU and previews particles
    //    through the debug-draw path. Kept as a fallback / debugging aid.
    struct VBIND_USERTYPE(name = ParticleEmitter, handle = ScriptParticleEmitterRef, accessor = particleEmitter)
        ParticleEmitterComponent
    {
        VBIND_FIELD() bool playing {true};
        VBIND_FIELD() bool worldSpace {true}; // simulate in world space (true) or local to the emitter (false)
        VBIND_FIELD() bool gpu {true};        // GPU compute backend (true) or CPU debug-draw fallback (false)

        VBIND_FIELD() uint32_t maxParticles {128};
        VBIND_FIELD() float    emissionRate {32.0f}; // particles spawned per second

        // Initial particle state (with per-particle random variance where noted).
        VBIND_FIELD() float     lifetime {2.0f};         // seconds
        VBIND_FIELD() float     lifetimeVariance {0.4f}; // +/- fraction of lifetime
        VBIND_FIELD() float     spawnRadius {0.1f};      // random spawn offset around the emitter
        VBIND_FIELD() glm::vec3 startVelocity {0.0f, 2.0f, 0.0f};
        VBIND_FIELD() float     velocityVariance {1.0f}; // random velocity magnitude added in a random direction
        VBIND_FIELD() glm::vec3 gravity {0.0f, -2.0f, 0.0f};

        // Appearance, interpolated over each particle's lifetime.
        VBIND_FIELD() float     startSize {0.12f};
        VBIND_FIELD() float     endSize {0.0f};
        VBIND_FIELD() glm::vec4 startColor {1.0f, 0.8f, 0.35f, 1.0f};
        VBIND_FIELD() glm::vec4 endColor {1.0f, 0.2f, 0.0f, 0.0f};
    };
} // namespace vultra
