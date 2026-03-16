#include "vultra/function/rendering/srp/builtin/features/meshlet_feature.hpp"

#include "vultra/function/rendering/srp/builtin/passes/build_indirect_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/coarse_instance_cull_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/meshlet_cull_pass.hpp"
#include "vultra/function/rendering/srp/builtin/resource_keys.hpp"

namespace vultra
{
    MeshletFeature::MeshletFeature()
    {
        m_CoarseInstanceCullPass = new CoarseInstanceCullPass();
        m_MeshletCullPass   = new MeshletCullPass();
        m_BuildIndirectPass = new BuildIndirectPass();
    }

    MeshletFeature::~MeshletFeature()
    {
        delete m_CoarseInstanceCullPass;
        delete m_MeshletCullPass;
        delete m_BuildIndirectPass;
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

        auto cullDone  = m_MeshletCullPass->addPass(ctx, coarseDone);
        auto buildDone = m_BuildIndirectPass->addPass(ctx, cullDone);
        ctx.data.set(kResKey_MeshletBuildDone, buildDone);
    }
} // namespace vultra
