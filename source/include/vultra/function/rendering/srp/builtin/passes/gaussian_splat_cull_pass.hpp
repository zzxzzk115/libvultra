#pragma once

#include "vultra/core/rhi/compute_pass.hpp"
#include "vultra/core/rhi/radix_sorter.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"

#include <fg/Fwd.hpp>

#include <optional>
#include <vector>

namespace vultra
{
    class GaussianSplatCullPass final : public rhi::ComputePass<GaussianSplatCullPass>
    {
        friend class BasePass;

    public:
        FrameGraphResource addPass(FrameGraphBuildContext& ctx, FrameGraphResource buildToken);

    private:
        rhi::ComputePipeline createPipeline(uint64_t variantHash) const;

    private:
        std::optional<rhi::RadixSorter> m_RadixSorter;
        uint32_t                        m_RadixSorterMaxElementCount {0};
        std::vector<uint32_t>           m_DrawPointBaseOffsets;
        std::vector<uint32_t>           m_DrawPointCounts;
        std::vector<uint32_t>           m_SortDrawIds;
    };
} // namespace vultra
