#pragma once

#include "vultra/core/rhi/compute_pass.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"

#include <fg/Fwd.hpp>

namespace vultra
{
    class BuildIndirectPass final : public rhi::ComputePass<BuildIndirectPass>
    {
        friend class BasePass;

    public:
        BuildIndirectPass();
        void addPass(FrameGraphBuildContext& ctx);

    private:
        rhi::ComputePipeline createPipeline(uint64_t variantHash) const;
    };
} // namespace vultra
