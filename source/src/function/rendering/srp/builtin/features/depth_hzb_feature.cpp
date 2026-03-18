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
        m_DepthPrePass->addPass(ctx);

        const auto depth = ctx.data.get(kResKey_DepthTexture);
        m_HzbGeneratePass->addPass(ctx, depth);
    }
} // namespace vultra
