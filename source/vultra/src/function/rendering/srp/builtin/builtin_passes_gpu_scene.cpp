#include "vultra/function/rendering/srp/builtin/builtin_pass_groups.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/draw_indirect_buffer.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/core/rhi/structs/framebuffer_info.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"
#include "vultra/function/framegraph/framegraph_import.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"
#include "vultra/function/rendering/render_structs.hpp"
#include "vultra/function/rendering/srp/builtin/builtin_pass_host.hpp"
#include "vultra/function/rendering/srp/builtin/passes/build_indirect_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/coarse_instance_cull_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/drawset_build_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/general_gaussian_splat_foveated_composite_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/general_gaussian_splat_preprocess_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/general_gaussian_splat_render_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/meshlet_cull_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/meshlet_hiz_cull_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/particle_render_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/particle_simulate_pass.hpp"
#include "vultra/function/rendering/srp/builtin/render_graph_backbuffer.hpp"
#include "vultra/function/rendering/srp/builtin/render_graph_resource_names.hpp"
#include "vultra/function/rendering/srp/builtin/resource_keys.hpp"
#include "vultra/function/rendering/srp/render_target_desc.hpp"
#include "vultra/function/services/render_service.hpp"

#include <fg/FrameGraph.hpp>
#include <nlohmann/json.hpp>
#include <vrendergraph/vrendergraph.hpp>

namespace vultra
{
    namespace
    {
        // ===== Self-registering builtin render-graph passes =====
        //
        // Each adapter OWNS the rhi pass object(s) it drives, declares its own node
        // port/param spec(s) in specs() (the single source of truth for slot names and
        // parameters), and holds its per-frame build body in build(). Owner state (live
        // build context, services, the per-frame tone-mapping flag) is reached through
        // BuiltinPassHost. The full catalog is composed by makeBuiltinRenderGraphPasses()
        // in builtin_render_graph_pass_factory.cpp from the append functions below.

        class CoarseInstanceCullBuiltin final : public IBuiltinRenderGraphPass
        {
        public:
            std::vector<BuiltinPassSpec> specs() const override
            {
                return {{"CoarseInstanceCull",
                         {},
                         {"visibleInstance", "visibleInstanceCount", "meshletCullDispatchArgs"},
                         {}}};
            }

            void build(BuiltinPassHost& host,
                       std::string_view,
                       const vrendergraph::ParamBlock&,
                       vrendergraph::PassBuildContext& passCtx) override
            {
                auto* ctx = host.currentBuildContext();
                if (!ctx)
                    return;
                m_Pass.addPass(*ctx);
                if (auto res = ctx->data.tryGet(kResKey_VisibleInstanceBuffer))
                    passCtx.setOutput("visibleInstance", res);
                if (auto res = ctx->data.tryGet(kResKey_VisibleInstanceCountBuffer))
                    passCtx.setOutput("visibleInstanceCount", res);
                if (auto res = ctx->data.tryGet(kResKey_MeshletCullDispatchArgsBuffer))
                    passCtx.setOutput("meshletCullDispatchArgs", res);
            }

        private:
            CoarseInstanceCullPass m_Pass;
        };

        class MeshletCullBuiltin final : public IBuiltinRenderGraphPass
        {
        public:
            std::vector<BuiltinPassSpec> specs() const override
            {
                return {{"MeshletCull",
                         {"visibleInstance", "visibleInstanceCount", "meshletCullDispatchArgs"},
                         {"visibleMeshlet", "visibleMeshletCount"},
                         {}}};
            }

            void build(BuiltinPassHost& host,
                       std::string_view,
                       const vrendergraph::ParamBlock&,
                       vrendergraph::PassBuildContext& passCtx) override
            {
                auto* ctx = host.currentBuildContext();
                if (!ctx)
                    return;
                m_Pass.addPass(*ctx);
                if (auto res = ctx->data.tryGet(kResKey_VisibleMeshletBuffer))
                    passCtx.setOutput("visibleMeshlet", res);
                if (auto res = ctx->data.tryGet(kResKey_VisibleMeshletCountBuffer))
                    passCtx.setOutput("visibleMeshletCount", res);
            }

        private:
            MeshletCullPass m_Pass;
        };

        class BuildIndirectBuiltin final : public IBuiltinRenderGraphPass
        {
        public:
            std::vector<BuiltinPassSpec> specs() const override
            {
                return {{"BuildIndirect",
                         {"visibleMeshlet", "visibleMeshletCount"},
                         {"draw",
                          "instance",
                          "meshTable",
                          "transform",
                          "meshlets",
                          "visibleMeshlet",
                          "visibleMeshletCount",
                          "materialTable"},
                         {}}};
            }

            void build(BuiltinPassHost& host,
                       std::string_view,
                       const vrendergraph::ParamBlock&,
                       vrendergraph::PassBuildContext& passCtx) override
            {
                auto* ctx = host.currentBuildContext();
                if (!ctx)
                    return;
                m_Pass.addPass(*ctx);
                if (auto res = ctx->data.tryGet(kResKey_DrawBuffer))
                    passCtx.setOutput("draw", res);
                if (auto res = ctx->data.tryGet(kResKey_InstanceBuffer))
                    passCtx.setOutput("instance", res);
                if (auto res = ctx->data.tryGet(kResKey_MeshTableBuffer))
                    passCtx.setOutput("meshTable", res);
                if (auto res = ctx->data.tryGet(kResKey_TransformBuffer))
                    passCtx.setOutput("transform", res);
                if (auto res = ctx->data.tryGet(kResKey_MeshletsBuffer))
                    passCtx.setOutput("meshlets", res);
                if (auto res = ctx->data.tryGet(kResKey_VisibleMeshletBuffer))
                    passCtx.setOutput("visibleMeshlet", res);
                if (auto res = ctx->data.tryGet(kResKey_VisibleMeshletCountBuffer))
                    passCtx.setOutput("visibleMeshletCount", res);
                if (auto res = ctx->data.tryGet(kResKey_MaterialTableBuffer))
                    passCtx.setOutput("materialTable", res);
            }

        private:
            BuildIndirectPass m_Pass;
        };

        class DrawsetBuildBuiltin final : public IBuiltinRenderGraphPass
        {
        public:
            std::vector<BuiltinPassSpec> specs() const override
            {
                return {{"DrawsetBuild", {"draw", "meshlets"}, {"draw", "meshlets", "indirect", "drawSet"}, {}}};
            }

            void build(BuiltinPassHost& host,
                       std::string_view,
                       const vrendergraph::ParamBlock&,
                       vrendergraph::PassBuildContext& passCtx) override
            {
                auto* ctx = host.currentBuildContext();
                if (!ctx)
                    return;
                m_Pass.addPass(*ctx);
                if (auto res = ctx->data.tryGet(kResKey_DrawBuffer))
                    passCtx.setOutput("draw", res);
                if (auto res = ctx->data.tryGet(kResKey_MeshletsBuffer))
                    passCtx.setOutput("meshlets", res);
                if (auto res = ctx->data.tryGet(kResKey_IndirectBuffer))
                    passCtx.setOutput("indirect", res);
                if (auto res = ctx->data.tryGet(kResKey_DrawSetBuffer))
                    passCtx.setOutput("drawSet", res);
            }

        private:
            DrawsetBuildPass m_Pass;
        };

        class MeshletHiZCullBuiltin final : public IBuiltinRenderGraphPass
        {
        public:
            std::vector<BuiltinPassSpec> specs() const override
            {
                return {{"MeshletHiZCull", {}, {"visibleMeshlet", "visibleMeshletCount"}, {}}};
            }

            void build(BuiltinPassHost& host,
                       std::string_view,
                       const vrendergraph::ParamBlock&,
                       vrendergraph::PassBuildContext& passCtx) override
            {
                auto* ctx = host.currentBuildContext();
                if (!ctx)
                    return;
                m_Pass.addPass(*ctx);
                if (auto res = ctx->data.tryGet(kResKey_VisibleMeshletBuffer))
                    passCtx.setOutput("visibleMeshlet", res);
                if (auto res = ctx->data.tryGet(kResKey_VisibleMeshletCountBuffer))
                    passCtx.setOutput("visibleMeshletCount", res);
            }

        private:
            MeshletHiZCullPass m_Pass;
        };

        // One object backs the standalone Preprocess/Render nodes and the fused
        // Composite node (preprocess + render in one) used by the simple path.
        class GaussianSplatBuiltin final : public IBuiltinRenderGraphPass
        {
        public:
            std::vector<BuiltinPassSpec> specs() const override
            {
                return {
                    {"GeneralGaussianSplatPreprocess",
                     {},
                     {"draw",
                      "packedSource",
                      "selectedSource",
                      "visibleSplat",
                      "sortKey",
                      "sortIndex",
                      "visibleCount",
                      "indirect",
                      "sortStorage",
                      "sh"},
                     {}},
                    {"GeneralGaussianSplatRender", {}, {"color"}, {}},
                    {"GeneralGaussianSplatComposite", {"source"}, {"color"}, {}},
                };
            }

            void build(BuiltinPassHost& host,
                       std::string_view type,
                       const vrendergraph::ParamBlock&,
                       vrendergraph::PassBuildContext& passCtx) override
            {
                auto* ctx = host.currentBuildContext();
                if (!ctx)
                    return;

                if (type == "GeneralGaussianSplatPreprocess")
                {
                    m_Preprocess.addPass(*ctx);
                    if (auto res = ctx->data.tryGet(kResKey_GeneralGaussianSplatDrawBuffer))
                        passCtx.setOutput("draw", res);
                    if (auto res = ctx->data.tryGet(kResKey_GeneralGaussianSplatPackedSourceBuffer))
                        passCtx.setOutput("packedSource", res);
                    if (auto res = ctx->data.tryGet(kResKey_GeneralGaussianSplatSelectedSourceBuffer))
                        passCtx.setOutput("selectedSource", res);
                    if (auto res = ctx->data.tryGet(kResKey_GeneralGaussianSplatVisibleSplatBuffer))
                        passCtx.setOutput("visibleSplat", res);
                    if (auto res = ctx->data.tryGet(kResKey_GeneralGaussianSplatSortKeyBuffer))
                        passCtx.setOutput("sortKey", res);
                    if (auto res = ctx->data.tryGet(kResKey_GeneralGaussianSplatSortIndexBuffer))
                        passCtx.setOutput("sortIndex", res);
                    if (auto res = ctx->data.tryGet(kResKey_GeneralGaussianSplatVisibleCountBuffer))
                        passCtx.setOutput("visibleCount", res);
                    if (auto res = ctx->data.tryGet(kResKey_GeneralGaussianSplatIndirectBuffer))
                        passCtx.setOutput("indirect", res);
                    if (auto res = ctx->data.tryGet(kResKey_GeneralGaussianSplatSortStorageBuffer))
                        passCtx.setOutput("sortStorage", res);
                    if (auto res = ctx->data.tryGet(kResKey_GeneralGaussianSplatShBuffer))
                        passCtx.setOutput("sh", res);
                    return;
                }

                if (type == "GeneralGaussianSplatRender")
                {
                    auto color = m_Render.addPass(*ctx);
                    if (color)
                    {
                        ctx->data.set(kResKey_FinalCompositionSource, color);
                        passCtx.setOutput("color", color);
                    }
                    return;
                }

                // GeneralGaussianSplatComposite: preprocess + render fused.
                const auto source       = passCtx.getInput("source");
                auto*      gpuSceneView = ctx->view().gpuSceneView;
                if (!gpuSceneView || !gpuSceneView->hasGeneralGaussianSplats())
                {
                    passCtx.setOutput("color", source);
                    return;
                }

                ctx->data.set(kResKey_FinalCompositionSource, source);
                m_Preprocess.addPass(*ctx);
                auto color = m_Render.addPass(*ctx);
                if (color)
                {
                    ctx->data.set(kResKey_FinalCompositionSource, color);
                    passCtx.setOutput("color", color);
                }
                else
                {
                    passCtx.setOutput("color", source);
                }
            }

        private:
            GeneralGaussianSplatPreprocessPass m_Preprocess;
            GeneralGaussianSplatRenderPass     m_Render;
        };

        class GaussianSplatFoveatedCompositeBuiltin final : public IBuiltinRenderGraphPass
        {
        public:
            std::vector<BuiltinPassSpec> specs() const override
            {
                return {{"GeneralGaussianSplatFoveatedComposite", {"fovea", "mid", "outer", "base"}, {"color"}, {}}};
            }

            void build(BuiltinPassHost& host,
                       std::string_view,
                       const vrendergraph::ParamBlock&,
                       vrendergraph::PassBuildContext& passCtx) override
            {
                auto* ctx = host.currentBuildContext();
                if (!ctx)
                    return;
                auto color = m_Pass.compose(*ctx,
                                            passCtx.getInput("fovea"),
                                            passCtx.getInput("mid"),
                                            passCtx.getInput("outer"),
                                            passCtx.getInput("base"));
                if (color)
                {
                    ctx->data.set(kResKey_FinalCompositionSource, color);
                    passCtx.setOutput("color", color);
                }
            }

        private:
            GeneralGaussianSplatFoveatedCompositePass m_Pass;
        };

        // One node, two collaborators: simulate (compute) then render (billboards).
        class ParticleBuiltin final : public IBuiltinRenderGraphPass
        {
        public:
            std::vector<BuiltinPassSpec> specs() const override
            {
                return {{"ParticleRender", {"source", "depth"}, {"color"}, {}}};
            }

            void build(BuiltinPassHost& host,
                       std::string_view,
                       const vrendergraph::ParamBlock&,
                       vrendergraph::PassBuildContext& passCtx) override
            {
                auto* ctx = host.currentBuildContext();
                if (!ctx)
                    return;

                const auto source = passCtx.getInput("source");
                const auto depth  = passCtx.getInput("depth");

                auto* gpuSceneView = ctx->view().gpuSceneView;
                if (!gpuSceneView || gpuSceneView->particleEmitters.empty())
                {
                    passCtx.setOutput("color", source);
                    return;
                }

                // Simulate (compute) then draw (billboards). The simulate pass returns
                // the per-emitter pool handles so the render pass reads them with a
                // correct compute-write -> vertex-read barrier.
                auto particleBuffers = m_Simulate.addPass(*ctx);
                auto color           = m_Render.addPass(*ctx, source, depth, particleBuffers);
                if (color)
                {
                    ctx->data.set(kResKey_FinalCompositionSource, color);
                    passCtx.setOutput("color", color);
                }
                else
                {
                    passCtx.setOutput("color", source);
                }
            }

        private:
            ParticleSimulatePass m_Simulate;
            ParticleRenderPass   m_Render;
        };
    } // namespace

    void appendGpuSceneBuiltinRenderGraphPasses(std::vector<std::unique_ptr<IBuiltinRenderGraphPass>>& passes)
    {
        passes.push_back(std::make_unique<CoarseInstanceCullBuiltin>());
        passes.push_back(std::make_unique<MeshletCullBuiltin>());
        passes.push_back(std::make_unique<BuildIndirectBuiltin>());
        passes.push_back(std::make_unique<DrawsetBuildBuiltin>());
        passes.push_back(std::make_unique<MeshletHiZCullBuiltin>());
        passes.push_back(std::make_unique<GaussianSplatBuiltin>());
        passes.push_back(std::make_unique<GaussianSplatFoveatedCompositeBuiltin>());
        passes.push_back(std::make_unique<ParticleBuiltin>());
    }
} // namespace vultra
