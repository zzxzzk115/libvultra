#include "vultra/function/rendering/srp/builtin/builtin_render_graph_pass.hpp"

#include <fg/FrameGraph.hpp>
#include <vrendergraph/vrendergraph.hpp>

#include <utility>

namespace vultra
{
    void IBuiltinRenderGraphPass::registerInto(vrendergraph::RenderGraphRegistry& registry, BuiltinPassHost& host)
    {
        for (auto& spec : specs())
        {
            // A scripted/custom pass of the same type registered earlier wins.
            if (registry.contains(spec.type))
                continue;

            const std::string type = spec.type;
            registry.registerPass(vrendergraph::PassDefinition {
                .type  = spec.type,
                .setup = [this, &host, type](FrameGraph&,
                                             FrameGraphBlackboard&,
                                             const vrendergraph::ParamBlock& params,
                                             vrendergraph::PassBuildContext& passCtx) {
                    build(host, type, params, passCtx);
                },
                .inputs  = std::move(spec.inputs),
                .outputs = std::move(spec.outputs),
                .params  = std::move(spec.params),
            });
        }
    }

    void IBuiltinRenderGraphPass::registerSpecsInto(vrendergraph::RenderGraphRegistry& registry) const
    {
        static const vrendergraph::PassSetupFn noop =
            [](FrameGraph&, FrameGraphBlackboard&, const vrendergraph::ParamBlock&, vrendergraph::PassBuildContext&) {};
        for (auto& spec : specs())
        {
            if (registry.contains(spec.type))
                continue;
            registry.registerPass(vrendergraph::PassDefinition {
                .type    = spec.type,
                .setup   = noop,
                .inputs  = std::move(spec.inputs),
                .outputs = std::move(spec.outputs),
                .params  = std::move(spec.params),
            });
        }
    }
} // namespace vultra
