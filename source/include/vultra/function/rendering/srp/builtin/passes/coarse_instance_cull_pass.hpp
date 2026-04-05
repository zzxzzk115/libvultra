#pragma once

#include "vultra/core/rhi/compute_pass.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"

#include <fg/Fwd.hpp>

namespace vultra
{
    class CoarseInstanceCullPass final : public rhi::ComputePass<CoarseInstanceCullPass>
    {
        friend class BasePass;

    public:
        CoarseInstanceCullPass();
        void addPass(FrameGraphBuildContext& ctx);

    private:
        rhi::ComputePipeline createPipeline(uint64_t variantHash) const;
    };
} // namespace vultra
