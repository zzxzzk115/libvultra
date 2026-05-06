#pragma once

#include "vultra/core/rhi/compute_pass.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"

#include <fg/Fwd.hpp>

namespace vultra
{
    class MeshletCullPass final : public rhi::ComputePass<MeshletCullPass>
    {
        friend class BasePass;

    public:
        MeshletCullPass();
        void addPass(FrameGraphBuildContext& ctx);

    private:
        rhi::ComputePipeline createPipeline(uint64_t variantHash) const;
    };
} // namespace vultra
