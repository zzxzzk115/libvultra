#include "vultra/function/rendering/srp/builtin/features/meshlet_feature.hpp"

#include "vultra/function/rendering/srp/builtin/passes/build_indirect_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/meshlet_cull_pass.hpp"
#include "vultra/function/rendering/srp/builtin/resource_keys.hpp"

namespace vultra
{
    MeshletFeature::MeshletFeature()
    {
        m_MeshletCullPass   = new MeshletCullPass();
        m_BuildIndirectPass = new BuildIndirectPass();
    }

    MeshletFeature::~MeshletFeature()
    {
        delete m_MeshletCullPass;
        delete m_BuildIndirectPass;
    }

    void MeshletFeature::addPasses(FrameGraphBuildContext& ctx)
    {
        auto cullDone  = m_MeshletCullPass->addPass(ctx);
        auto buildDone = m_BuildIndirectPass->addPass(ctx, cullDone);
        ctx.data.set(kResKey_MeshletBuildDone, buildDone);
    }
} // namespace vultra
