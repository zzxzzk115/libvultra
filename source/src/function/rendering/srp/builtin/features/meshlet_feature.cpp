#include "vultra/function/rendering/srp/builtin/features/meshlet_feature.hpp"

#include "vultra/function/rendering/srp/builtin/passes/build_indirect_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/coarse_instance_cull_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/depth_pre_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/drawset_build_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/meshlet_cull_pass.hpp"
#include "vultra/function/rendering/srp/builtin/resource_keys.hpp"

namespace vultra
{
    MeshletFeature::MeshletFeature()
    {
        m_CoarseInstanceCullPass = new CoarseInstanceCullPass();
        m_MeshletCullPass   = new MeshletCullPass();
        m_BuildIndirectPass = new BuildIndirectPass();
        m_DrawsetBuildPass = new DrawsetBuildPass();
        m_DepthPrePass = new DepthPrePass();
    }

    MeshletFeature::~MeshletFeature()
    {
        delete m_CoarseInstanceCullPass;
        delete m_MeshletCullPass;
        delete m_BuildIndirectPass;
        delete m_DrawsetBuildPass;
        delete m_DepthPrePass;
    }

    void MeshletFeature::addPasses(FrameGraphBuildContext& ctx)
    {
        auto* gpuSceneView = ctx.view().gpuSceneView;
        if (!gpuSceneView)
            return;

        const bool hasMeshletDraws = gpuSceneView->isGpuDriven() ? (gpuSceneView->maxDraws > 0u) :
                                                                   (gpuSceneView->countMeshletDraws() > 0u);
        if (!hasMeshletDraws)
            return;

        auto coarseDone = m_CoarseInstanceCullPass->addPass(ctx);
        ctx.data.set(kResKey_CoarseInstanceCullDone, coarseDone);

        // Stage A: visibility cull and final drawset build for this frame.
        auto cullDone = m_MeshletCullPass->addPass(ctx, coarseDone);
        // Skip HiZ/HZB for now: build final drawset directly from frustum-cull output.
        auto buildDone = m_BuildIndirectPass->addPass(ctx, cullDone);
        auto drawsetDone = m_DrawsetBuildPass->addPass(ctx, buildDone);
        ctx.data.set(kResKey_MeshletBuildDone, drawsetDone);

        // Stage B: meshlet depth prepass and HZB build for frame-latent occlusion.
        const auto depth = m_DepthPrePass->addPass(ctx);
        ctx.data.set(kResKey_DepthTexture, depth);
    }
} // namespace vultra
