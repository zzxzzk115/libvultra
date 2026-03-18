#pragma once

#include "vultra/core/rhi/compute_pass.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"

namespace vultra
{
    class DrawsetBuildPass final : public rhi::ComputePass<DrawsetBuildPass>
    {
        friend class BasePass;

    public:
        void addPass(FrameGraphBuildContext& ctx);

    private:
        rhi::ComputePipeline createPipeline(uint64_t variantHash) const;
    };
} // namespace vultra
