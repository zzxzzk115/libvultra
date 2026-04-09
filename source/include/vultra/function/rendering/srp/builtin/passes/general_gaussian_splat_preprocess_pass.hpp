#pragma once

#include "vultra/core/rhi/compute_pass.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"

namespace vultra
{
    class GeneralGaussianSplatPreprocessPass final : public rhi::ComputePass<GeneralGaussianSplatPreprocessPass>
    {
    public:
        GeneralGaussianSplatPreprocessPass();

        void addPass(FrameGraphBuildContext& ctx);

        rhi::ComputePipeline createPipeline(bool useMultiview) const;
        rhi::ComputePipeline createPipeline(uint64_t variantHash) const;
    };
} // namespace vultra
