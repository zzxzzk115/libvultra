#include "vultra/function/rendering/srp/builtin/features/depth_hzb_feature.hpp"

#include "vultra/function/rendering/srp/builtin/passes/depth_pre_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/hzb_generate_pass.hpp"
#include "vultra/function/rendering/srp/builtin/resource_keys.hpp"

#include <memory>

namespace vultra
{
    DepthHzbFeature::DepthHzbFeature()
    {
        m_DepthPrePass    = std::make_unique<DepthPrePass>();
        m_HzbGeneratePass = std::make_unique<HzbGeneratePass>();
    }

    DepthHzbFeature::~DepthHzbFeature() = default;

    void DepthHzbFeature::addPasses(FrameGraphBuildContext& ctx)
    {
        m_DepthPrePass->addPass(ctx);

        const auto depth = ctx.data.get(kResKey_DepthTexture);
        m_HzbGeneratePass->addPass(ctx, depth);
    }
} // namespace vultra
