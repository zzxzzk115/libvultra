#include "vultra/function/rendering/srp/builtin/features/depth_hzb_feature.hpp"

#include "vultra/function/rendering/srp/builtin/passes/depth_pre_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/hzb_generate_pass.hpp"
#include "vultra/function/rendering/srp/builtin/resource_keys.hpp"

namespace vultra
{
    DepthHzbFeature::DepthHzbFeature()
    {
        m_DepthPrePass    = new DepthPrePass();
        m_HzbGeneratePass = new HzbGeneratePass();
    }

    DepthHzbFeature::~DepthHzbFeature()
    {
        delete m_DepthPrePass;
        delete m_HzbGeneratePass;
    }

    void DepthHzbFeature::addPasses(FrameGraphBuildContext& ctx)
    {
        const auto depth = m_DepthPrePass->addPass(ctx);
        ctx.data.set(kResKey_DepthTexture, depth);

        const auto depthDone = ctx.data.get(kResKey_DepthPreDone);
        const auto hzb       = m_HzbGeneratePass->addPass(ctx, depth, depthDone);

        ctx.data.set(kResKey_HzbTexture, hzb);
    }
} // namespace vultra
