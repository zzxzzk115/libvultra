#pragma once

#include "vultra/core/rhi/compute_pass.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"

#include <vector>

namespace vultra
{
    // Compute simulation for GPU particle emitters. Adds one dispatch per emitter (read from the
    // active GpuSceneView::particleEmitters), each updating that emitter's persistent particle pool
    // SSBO. Returns the per-emitter frame-graph buffer handles (parallel to particleEmitters) so the
    // ParticleRenderPass can read them with a correct compute-write -> vertex-read barrier.
    class ParticleSimulatePass final : public rhi::ComputePass<ParticleSimulatePass>
    {
        friend class BasePass;

    public:
        ParticleSimulatePass();

        [[nodiscard]] std::vector<FrameGraphResource> addPass(FrameGraphBuildContext& ctx);

    private:
        rhi::ComputePipeline createPipeline(uint64_t variantHash) const;
    };
} // namespace vultra
