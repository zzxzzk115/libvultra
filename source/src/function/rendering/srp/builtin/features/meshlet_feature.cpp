#include "vultra/function/rendering/srp/builtin/features/meshlet_feature.hpp"

#include "vultra/function/framegraph/framegraph_import.hpp"
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
        m_MeshletCullPass        = new MeshletCullPass();
        m_BuildIndirectPass      = new BuildIndirectPass();
        m_DrawsetBuildPass       = new DrawsetBuildPass();
        m_DepthPrePass           = new DepthPrePass();
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
        auto* gpuSceneDatabase = ctx.view().gpuSceneDatabase;
        auto* gpuSceneView     = ctx.view().gpuSceneView;
        if (!gpuSceneDatabase || !gpuSceneView || !gpuSceneDatabase->resources)
            return;

        const bool hasMeshletDraws =
            gpuSceneView->isGpuDriven() ? (gpuSceneView->maxDraws > 0u) : (gpuSceneView->countMeshletDraws() > 0u);
        if (!hasMeshletDraws)
            return;

        const auto importStorageBuffer = [&ctx](const char* name, rhi::Buffer* buffer) -> FrameGraphResource {
            if (!buffer || !(*buffer))
                return {};
            return framegraph::importBuffer(ctx.fg, name, buffer, framegraph::BufferType::eStorageBuffer);
        };

        // Import only persistent scene-owned buffers. All GPU-driven intermediate resources
        // (visible lists, counters, dispatch args, draw buffers, indirect buffers, draw-set counts)
        // are created as transient FrameGraph resources by the producer passes.
        auto instanceBuffer  = importStorageBuffer("ImportedInstanceBuffer", gpuSceneDatabase->instanceBuffer.get());
        auto meshTableBuffer = importStorageBuffer("ImportedMeshTableBuffer", gpuSceneDatabase->meshTableBuffer.get());
        auto transformBuffer = importStorageBuffer("ImportedTransformBuffer", gpuSceneDatabase->transformBuffer.get());
        auto meshletsBuffer =
            importStorageBuffer("ImportedMeshletsBuffer", gpuSceneDatabase->resources->meshlets.meshletsBuffer.get());
        auto materialTableBuffer =
            importStorageBuffer("ImportedMaterialTableBuffer", gpuSceneDatabase->resources->materialTableBuffer.get());
        auto materialParamsBuffer =
            importStorageBuffer("ImportedMaterialParamsBuffer", gpuSceneDatabase->resources->materialParams.gpu.get());
        auto meshletVertexBuffer = importStorageBuffer(
            "ImportedMeshletVertexBuffer", gpuSceneDatabase->resources->meshlets.meshletVerticesBuffer.get());
        auto meshletTriangleBuffer = importStorageBuffer(
            "ImportedMeshletTriangleBuffer", gpuSceneDatabase->resources->meshlets.meshletTrianglesBuffer.get());

        // Make imported persistent buffers available to passes via data context.
        ctx.data.set(kResKey_InstanceBuffer, instanceBuffer);
        ctx.data.set(kResKey_MeshTableBuffer, meshTableBuffer);
        ctx.data.set(kResKey_MeshletsBuffer, meshletsBuffer);
        ctx.data.set(kResKey_TransformBuffer, transformBuffer);
        ctx.data.set(kResKey_MaterialTableBuffer, materialTableBuffer);
        ctx.data.set(kResKey_MaterialParametersBuffer, materialParamsBuffer);
        ctx.data.set(kResKey_MeshletVertexBuffer, meshletVertexBuffer);
        ctx.data.set(kResKey_MeshletTriangleBuffer, meshletTriangleBuffer);

        m_CoarseInstanceCullPass->addPass(ctx);

        // Stage A: visibility cull and final drawset build for this frame.
        m_MeshletCullPass->addPass(ctx);
        // Skip HiZ/HZB for now: build final drawset directly from frustum-cull output.
        m_BuildIndirectPass->addPass(ctx);
        m_DrawsetBuildPass->addPass(ctx);

        // Stage B: meshlet depth prepass and HZB build for frame-latent occlusion.
        m_DepthPrePass->addPass(ctx);
    }
} // namespace vultra
