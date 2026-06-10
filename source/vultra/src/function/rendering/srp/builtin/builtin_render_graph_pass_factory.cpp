#include "vultra/function/rendering/srp/builtin/builtin_pass_groups.hpp"
#include "vultra/function/rendering/srp/builtin/builtin_render_graph_pass.hpp"
#include "vultra/function/rendering/srp/declarative_renderer.hpp"

namespace vultra
{
    // The single place that lists every builtin render-graph pass. Each group TU
    // (builtin_passes_*.cpp) owns its adapters; this factory composes the catalog used
    // by both the runtime registration loop and the editor node palette.
    std::vector<std::unique_ptr<IBuiltinRenderGraphPass>> makeBuiltinRenderGraphPasses()
    {
        std::vector<std::unique_ptr<IBuiltinRenderGraphPass>> passes;
        passes.reserve(30);
        appendSceneBuiltinRenderGraphPasses(passes);
        appendPostProcessBuiltinRenderGraphPasses(passes);
        appendGpuSceneBuiltinRenderGraphPasses(passes);
        return passes;
    }

    void registerBuiltinRenderGraphPasses(vrendergraph::RenderGraphRegistry& registry)
    {
        // Editor node palette: ports/params only (no-op setups).
        for (auto& pass : makeBuiltinRenderGraphPasses())
            pass->registerSpecsInto(registry);
    }
} // namespace vultra
