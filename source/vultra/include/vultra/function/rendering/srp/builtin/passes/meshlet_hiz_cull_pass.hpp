#pragma once

#include "vultra/core/rhi/compute_pass.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"

namespace vultra
{
    class MeshletHiZCullPass final : public rhi::ComputePass<MeshletHiZCullPass>
    {
        friend class BasePass;

    public:
        MeshletHiZCullPass();
        void addPass(FrameGraphBuildContext& ctx);

    private:
        rhi::ComputePipeline createPipeline(uint64_t variantHash) const;
    };
} // namespace vultra
