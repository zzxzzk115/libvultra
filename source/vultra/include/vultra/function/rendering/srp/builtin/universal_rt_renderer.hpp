#pragma once

#include "vultra/function/rendering/srp/renderer.hpp"

#include <memory>

namespace vultra
{
    class DeclarativeRenderer;

    class UniversalRtRenderer final : public Renderer
    {
    public:
        UniversalRtRenderer();
        ~UniversalRtRenderer() override;

        std::string_view name() const override { return "universal_rt"; }

        void init() override;
        void buildFrameGraph(FrameGraphBuildContext& ctx) override;

    private:
        std::unique_ptr<DeclarativeRenderer> m_GraphRenderer;
    };
} // namespace vultra
