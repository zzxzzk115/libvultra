#include "vultra/function/rendering/srp/builtin/passes/legacy_basecolor_pass.hpp"

#include "vultra/function/rendering/srp/builtin/passes/android_basecolor_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/webgpu_basecolor_pass.hpp"

namespace vultra
{
    LegacyBaseColorPass::LegacyBaseColorPass(const LegacyRendererProfile profile) : m_Profile(profile)
    {
        m_AndroidBaseColorPass = new AndroidBaseColorPass();
        m_WebGPUBaseColorPass  = new WebGPUBaseColorPass();
    }

    LegacyBaseColorPass::~LegacyBaseColorPass()
    {
        delete m_WebGPUBaseColorPass;
        delete m_AndroidBaseColorPass;
    }

    FrameGraphResource LegacyBaseColorPass::addPass(FrameGraphBuildContext& ctx, const FrameGraphResource target)
    {
        if (m_Profile == LegacyRendererProfile::eWebGPUCompat)
        {
            if (target)
            {
                m_WebGPUBaseColorPass->addPass(ctx, target);
                return target;
            }
            return {};
        }

        return m_AndroidBaseColorPass->addPass(ctx);
    }
} // namespace vultra

