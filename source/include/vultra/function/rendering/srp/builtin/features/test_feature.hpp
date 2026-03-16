#pragma once

#include "vultra/function/rendering/srp/render_feature.hpp"

namespace vultra
{
    class TestPass;
    class SplatCompositePass;

    class TestFeature : public RenderFeature
    {
    public:
        TestFeature();
        ~TestFeature();

        DEFINE_RENDER_FEATURE(TestFeature);

        virtual void addPasses(FrameGraphBuildContext& ctx) override;

    private:
        TestPass*            m_TestPass {nullptr};
        SplatCompositePass*  m_SplatCompositePass {nullptr};
    };
} // namespace vultra