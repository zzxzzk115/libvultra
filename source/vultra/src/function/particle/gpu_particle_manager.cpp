#include "vultra/function/particle/gpu_particle_manager.hpp"

#include "vultra/core/rhi/render_device.hpp"
#include "vultra/core/rhi/storage_buffer.hpp"
#include "vultra/function/rendering/render_structs.hpp"

#include <algorithm>
#include <functional>

namespace vultra
{
    namespace
    {
        // Bound the work even if a scene authors absurd values.
        constexpr uint32_t kMaxParticlesPerEmitter = 1u << 20; // 1M slots
        constexpr std::size_t kMaxEmitters         = 64;       // per-frame dispatch/draw budget
    } // namespace

    void GpuParticleManager::update(const RenderWorld& world, rhi::RenderDevice& rd, const float dt,
                                    const uint64_t frameIndex)
    {
        auto* view = world.gpuSceneView;
        if (view != nullptr)
            view->particleEmitters.clear();

        const auto frameSeed = static_cast<uint32_t>(frameIndex);

        for (const auto& re : world.emitters)
        {
            const auto& emitter = re.emitter;
            if (!emitter.gpu)
                continue; // CPU-backed emitters are handled by ParticleSystem.
            if (view != nullptr && view->particleEmitters.size() >= kMaxEmitters)
                break;

            const uint32_t maxParticles = std::clamp<uint32_t>(emitter.maxParticles, 1u, kMaxParticlesPerEmitter);

            auto& state = m_States[re.entity];
            if (!state.buffer || state.maxParticles != maxParticles)
            {
                const uint64_t bytes   = static_cast<uint64_t>(maxParticles) * sizeof(GpuParticle);
                state.buffer           = createRef<rhi::StorageBuffer>(rd.createStorageBuffer(bytes));
                state.maxParticles     = maxParticles;
                state.spawnCursor      = 0u;
                state.spawnAccumulator = 0.0f;
                state.needsReset       = true; // pool memory is uninitialised; let the shader seed it.
            }

            // Advance emission at most once per frame, even if several render worlds (e.g. the editor
            // scene view and game view) share this emitter. Extra views republish the cached push
            // constants so they render the same simulation state.
            if (state.lastAdvancedFrame != frameIndex)
            {
                uint32_t emitCount = 0u;
                if (emitter.playing && emitter.emissionRate > 0.0f && dt > 0.0f)
                {
                    state.spawnAccumulator += emitter.emissionRate * dt;
                    // A long hitch must not try to spawn more than the whole pool in a single frame.
                    state.spawnAccumulator = std::min(state.spawnAccumulator, static_cast<float>(maxParticles));
                    emitCount              = static_cast<uint32_t>(state.spawnAccumulator);
                    state.spawnAccumulator -= static_cast<float>(emitCount);
                    emitCount = std::min(emitCount, maxParticles);
                }

                ParticleEmitterPushConstants pc {};
                pc.originAndDt       = glm::vec4(re.origin, dt);
                pc.startVelAndRadius = glm::vec4(emitter.startVelocity, emitter.spawnRadius);
                pc.gravityAndVelVar  = glm::vec4(emitter.gravity, emitter.velocityVariance);
                pc.lifeAndSizes =
                    glm::vec4(emitter.lifetime, emitter.lifetimeVariance, emitter.startSize, emitter.endSize);
                pc.startColor = emitter.startColor;
                pc.endColor   = emitter.endColor;

                uint32_t cursorWord = state.spawnCursor & kCursorMask;
                if (state.needsReset)
                {
                    cursorWord |= kResetBit;
                    state.needsReset = false;
                }
                const uint32_t entitySeed = static_cast<uint32_t>(std::hash<CoreUUID> {}(re.entity));
                pc.counts = glm::uvec4(maxParticles, emitCount, cursorWord, frameSeed ^ entitySeed);

                state.lastPc            = pc;
                state.spawnCursor       = (state.spawnCursor + emitCount) % maxParticles;
                state.lastAdvancedFrame = frameIndex;
            }

            state.lastFrameSeen = frameIndex;
            if (view != nullptr)
                view->particleEmitters.push_back(
                    GpuParticleEmitterDraw {state.buffer.get(), maxParticles, state.lastPc});
        }

        // Drop pools for emitters not seen in any render world for a few frames.
        for (auto it = m_States.begin(); it != m_States.end();)
        {
            if (frameIndex - it->second.lastFrameSeen > 4u)
                it = m_States.erase(it);
            else
                ++it;
        }
    }
} // namespace vultra
