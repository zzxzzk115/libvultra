#pragma once

#include <glm/vec4.hpp>

#include <cstdint>

namespace vultra
{
    namespace rhi
    {
        class StorageBuffer;
    }

    // One GPU particle. std430-friendly (two vec4, 32 bytes). Mirrored in
    // builtin/shaders/passes/highend/particle_*.vshader as:
    //   struct GpuParticle { vec4 positionAge; vec4 velocityLife; };
    // A slot is "dead" when velocityLife.w (lifetime) <= 0.
    struct GpuParticle
    {
        glm::vec4 positionAge {0.0f};  // xyz = world position, w = age (seconds)
        glm::vec4 velocityLife {0.0f}; // xyz = velocity, w = lifetime (seconds; <= 0 => dead)
    };
    static_assert(sizeof(GpuParticle) == 32, "GpuParticle must stay 32 bytes (std430 layout)");

    // Per-emitter push constants, shared by the simulate (compute) and billboard (graphics) shaders.
    // Packed into vec4/uvec4 to keep std140/std430 push-constant layout trivially portable and small
    // (112 bytes, within the 128-byte guaranteed push-constant range).
    struct ParticleEmitterPushConstants
    {
        glm::vec4  originAndDt {0.0f};       // xyz = emitter world origin, w = dt (seconds)
        glm::vec4  startVelAndRadius {0.0f}; // xyz = start velocity, w = spawn radius
        glm::vec4  gravityAndVelVar {0.0f};  // xyz = gravity, w = velocity variance
        glm::vec4  lifeAndSizes {0.0f};      // x = lifetime, y = lifetimeVariance, z = startSize, w = endSize
        glm::vec4  startColor {1.0f};        // rgba at birth
        glm::vec4  endColor {1.0f};          // rgba at death
        glm::uvec4 counts {0u};              // x = maxParticles, y = emitCount, z = spawnCursor, w = frameSeed
    };
    static_assert(sizeof(ParticleEmitterPushConstants) == 112,
                  "ParticleEmitterPushConstants must match the shader push_constant block");

    // Per-frame, per-emitter draw record published by GpuParticleManager onto the active GpuSceneView
    // so the render-graph particle passes can simulate and draw each emitter. The buffer is owned by
    // GpuParticleManager (stable across frames); this only carries a non-owning view of it.
    struct GpuParticleEmitterDraw
    {
        rhi::StorageBuffer*          buffer {nullptr};
        uint32_t                     maxParticles {0};
        ParticleEmitterPushConstants pc {};
    };
} // namespace vultra
