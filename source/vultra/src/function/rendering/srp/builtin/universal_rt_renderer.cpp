#include "vultra/function/rendering/srp/builtin/universal_rt_renderer.hpp"

#include "vultra/function/rendering/srp/declarative_renderer.hpp"

namespace vultra
{
    UniversalRtRenderer::UniversalRtRenderer() = default;

    UniversalRtRenderer::~UniversalRtRenderer() = default;

    void UniversalRtRenderer::init()
    {
        auto* services = getServices();
        if (!services)
            return;

        m_GraphRenderer = createScope<DeclarativeRenderer>("builtin://render/universal_rt.vrg.json", "universal_rt");
        m_GraphRenderer->setupServices(*services);
        m_GraphRenderer->init();
    }

    void UniversalRtRenderer::buildFrameGraph(FrameGraphBuildContext& ctx)
    {
        if (m_GraphRenderer)
            m_GraphRenderer->buildFrameGraph(ctx);
    }
} // namespace vultra
