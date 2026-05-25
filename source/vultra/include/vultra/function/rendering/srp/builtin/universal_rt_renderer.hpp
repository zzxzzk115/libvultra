#pragma once

#include "vultra/function/rendering/srp/builtin/passes/final_composition_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/raytracing_primary_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/tone_mapping_pass.hpp"
#include "vultra/function/rendering/srp/renderer.hpp"

namespace vultra
{
    class UniversalRtRenderer final : public Renderer
    {
    public:
        std::string_view name() const override { return "universal_rt"; }

        void buildFrameGraph(FrameGraphBuildContext& ctx) override;

    private:
        RayTracingPrimaryPass m_PrimaryPass;
        ToneMappingPass       m_ToneMappingPass;
        FinalCompositionPass  m_FinalCompositionPass;
    };
} // namespace vultra
