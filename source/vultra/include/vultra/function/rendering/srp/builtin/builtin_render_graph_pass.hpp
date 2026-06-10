#pragma once

#include <vrendergraph/vrendergraph.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace vultra
{
    class BuiltinPassHost;

    // One graph-node spec: the node type plus its input/output slot names and
    // editable parameter descriptors. Replaces a single legacy
    // declareBuiltinRenderGraphPasses() catalog entry.
    struct BuiltinPassSpec
    {
        std::string                          type;
        std::vector<std::string>             inputs;
        std::vector<std::string>             outputs;
        std::vector<vrendergraph::ParamDesc> params;
    };

    // A self-registering builtin render-graph pass: it owns the rhi pass object(s)
    // it drives, declares its own port/param spec(s), and builds itself into the
    // frame graph. The runtime just does `for (p : passes) p->registerInto(reg, host)`;
    // the editor node palette uses registerSpecsInto() for ports/params only.
    //
    // A pass and its registration are no longer split across two far-apart catalogs:
    // specs() (was declareBuiltinRenderGraphPasses) and build() (was a registerBuiltin
    // lambda) live together on one object, so a new pass can't be half-registered.
    class IBuiltinRenderGraphPass
    {
    public:
        virtual ~IBuiltinRenderGraphPass() = default;

        // Source of truth for this pass's node port/param layout. Most passes return
        // exactly one spec; a few composite passes back several node types with one
        // object and return several specs (disambiguated by `type` in build()).
        [[nodiscard]] virtual std::vector<BuiltinPassSpec> specs() const = 0;

        // The real per-frame build body (was a registerBuiltin(...) lambda). `type`
        // disambiguates when specs() returned more than one entry.
        virtual void build(BuiltinPassHost&                host,
                           std::string_view                type,
                           const vrendergraph::ParamBlock& params,
                           vrendergraph::PassBuildContext& passCtx) = 0;

        // Registers every spec with a real setup callback bound to build(). Honors
        // precedence: a pass type already present (scripted/custom) is left untouched.
        void registerInto(vrendergraph::RenderGraphRegistry& registry, BuiltinPassHost& host);

        // Registers every spec with a no-op setup (editor node palette: ports/params only).
        void registerSpecsInto(vrendergraph::RenderGraphRegistry& registry) const;
    };

    // The builtin pass catalog: constructs one adapter per registration group, in a
    // stable order. Used by BOTH the runtime registration loop and the editor palette
    // so port/param layout lives in exactly one place (each adapter's specs()).
    [[nodiscard]] std::vector<std::unique_ptr<IBuiltinRenderGraphPass>> makeBuiltinRenderGraphPasses();
} // namespace vultra
