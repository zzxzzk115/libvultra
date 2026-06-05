#pragma once

#include "vultra/core/base/base.hpp"
#include "vultra/core/base/uuid.hpp"
#include "vultra/function/particle/gpu_particle.hpp"

#include <cstdint>
#include <unordered_map>

namespace vultra
{
    namespace rhi
    {
        class RenderDevice;
        class StorageBuffer;
    } // namespace rhi

    struct RenderWorld;

    // Owns the persistent GPU state for compute-simulated particle emitters.
    //
    // For every GPU emitter (gpu == true) it keeps a fixed-size particle pool SSBO that survives
    // across frames, advances a CPU emission accumulator + round-robin spawn cursor, and each frame
    // publishes a per-emitter draw record (buffer + push constants) onto the active GpuSceneView for
    // the render-graph particle passes to consume. A freshly created/resized pool is flagged for a
    // one-time GPU-side reset (encoded in the push constants) so the simulate shader initialises it
    // without any host-side clear.
    class GpuParticleManager
    {
    public:
        // Refreshes per-emitter pools and publishes world.gpuSceneView->particleEmitters. May be
        // called multiple times per frame (once per render world being rendered — main + overrides);
        // emission for a given emitter is advanced at most once per `frameIndex`, so calling it for
        // several worlds that share emitters does not multiply the spawn rate. Pass the real frame
        // delta and the monotonically increasing render frame index.
        void update(const RenderWorld& world, rhi::RenderDevice& rd, float dt, uint64_t frameIndex);

        void clear() { m_States.clear(); }

        // Reset bit packed into ParticleEmitterPushConstants::counts.z (spawn cursor word).
        static constexpr uint32_t kResetBit  = 0x80000000u;
        static constexpr uint32_t kCursorMask = 0x7FFFFFFFu;

    private:
        struct EmitterState
        {
            Ref<rhi::StorageBuffer>      buffer;
            float                        spawnAccumulator {0.0f};
            uint32_t                     spawnCursor {0};
            uint32_t                     maxParticles {0};
            bool                         needsReset {true};
            ParticleEmitterPushConstants lastPc {};                  // cached this frame for republish to extra views
            uint64_t                     lastAdvancedFrame {UINT64_MAX};
            uint64_t                     lastFrameSeen {0};
        };

        std::unordered_map<CoreUUID, EmitterState> m_States;
    };
} // namespace vultra
