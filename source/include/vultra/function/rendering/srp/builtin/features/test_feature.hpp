#pragma once

#include "vultra/function/rendering/srp/builtin/passes/test_pass.hpp"
#include "vultra/function/rendering/srp/render_feature.hpp"

namespace vultra
{
    class TestFeature : public RenderFeature
    {
    public:
        DEFINE_RENDER_FEATURE(TestFeature);

        virtual void addPasses(FrameGraphBuildContext& ctx) override;

    private:
        TestPass m_TestPass;
    };
} // namespace vultra