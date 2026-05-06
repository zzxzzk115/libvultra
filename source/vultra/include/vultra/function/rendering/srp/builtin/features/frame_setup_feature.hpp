#pragma once

#include "vultra/function/rendering/srp/render_feature.hpp"

namespace vultra
{
    class FrameSetupFeature : public RenderFeature
    {
    public:
        DEFINE_RENDER_FEATURE(FrameSetupFeature);

        virtual void addPasses(FrameGraphBuildContext& ctx) override;
    };
} // namespace vultra