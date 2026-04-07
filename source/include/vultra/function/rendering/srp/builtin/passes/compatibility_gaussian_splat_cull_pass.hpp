#pragma once

#include "vultra/core/rhi/compute_pass.hpp"
#include "vultra/core/rhi/radix_sorter.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"
#include "vultra/function/rendering/srp/builtin/features/gaussian_splat_feature.hpp"

#include <fg/Fwd.hpp>

#include <optional>

namespace vultra
{
    class CompatibilityGaussianSplatCullPass final : public rhi::ComputePass<CompatibilityGaussianSplatCullPass>
    {
        friend class BasePass;

    public:
        CompatibilityGaussianSplatCullPass();
        FrameGraphResource addPass(FrameGraphBuildContext&              ctx,
                                   FrameGraphResource                   buildToken,
                                   const GaussianSplatRendererSettings& settings);

    private:
        rhi::ComputePipeline createPipeline(uint64_t variantHash) const;

    private:
        std::optional<rhi::RadixSorter> m_RadixSorter;
        uint32_t                        m_RadixSorterMaxElementCount {0};
        bool                            m_HasLoggedDispatchClamp {false};
        bool                            m_HasLoggedMemoryClamp {false};
    };
} // namespace vultra
