#pragma once

#include "vultra/core/rhi/compute_pass.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"

namespace vultra
{
    class HzbGeneratePass final : public rhi::ComputePass<HzbGeneratePass>
    {
        friend class BasePass;

    public:
        void addPass(FrameGraphBuildContext& ctx, FrameGraphResource depth);

    private:
        rhi::ComputePipeline createPipeline(uint64_t variantHash) const;
    };
} // namespace vultra
