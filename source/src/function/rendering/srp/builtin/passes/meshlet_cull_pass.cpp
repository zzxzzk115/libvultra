#include "vultra/function/rendering/srp/builtin/passes/meshlet_cull_pass.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/function/framegraph/framegraph_buffer.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/rendering/srp/builtin/resource_keys.hpp"

#include <fg/FrameGraph.hpp>

namespace vultra
{
    namespace
    {
        constexpr auto PASS_NAME = "MeshletCullPass";

        struct CullPushConstants
        {
            uint32_t instanceCount {0};
            uint32_t maxVisibleMeshlets {0};
            uint32_t enableConeCull {0};
            uint32_t padding0 {0};
        };
    } // namespace

    FrameGraphResource MeshletCullPass::addPass(FrameGraphBuildContext& ctx)
    {
        struct PassData
        {
            FrameGraphResource camera;
            FrameGraphResource token;
        };

        const auto cameraBlock = ctx.bb.get<CameraData>().cameraBlock.fgResource;

        auto*      gpuSceneDatabase = ctx.view().gpuSceneDatabase;
        auto*      gpuSceneView     = ctx.view().gpuSceneView;
        const auto instanceCount    = gpuSceneDatabase ? static_cast<uint32_t>(gpuSceneDatabase->instances.size()) : 0u;
        const auto maxVisible       = gpuSceneView ? gpuSceneView->maxVisibleMeshlets : 0u;

        auto data = ctx.fg.addCallbackPass<PassData>(
            PASS_NAME,
            [cameraBlock](FrameGraph::Builder& builder, PassData& pd) {
                PASS_SETUP_ZONE;

                pd.camera = builder.read(cameraBlock,
                                         framegraph::BindingInfo {
                                             .location      = {.set = 0, .binding = 0},
                                             .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                         });

                pd.token =
                    builder.create<framegraph::FrameGraphBuffer>("MeshletCullToken",
                                                                 {
                                                                     .type     = framegraph::BufferType::eStorageBuffer,
                                                                     .stride   = sizeof(uint32_t),
                                                                     .capacity = 1,
                                                                 });
                pd.token = builder.write(pd.token,
                                         framegraph::BindingInfo {
                                             .location      = {.set = 0, .binding = 31},
                                             .pipelineStage = framegraph::PipelineStage::eTransfer,
                                         });
            },
            [this, instanceCount, maxVisible](const PassData& pd, FrameGraphPassResources& resources, void* ctxPtr) {
                auto& rc = *static_cast<FrameGraphExecContext*>(ctxPtr);
                setRenderDevice(rc.rd);
                if (!rc.ext.builtinShaderLib)
                    return;
                setShaderLib(*rc.ext.builtinShaderLib);

                RHI_GPU_ZONE(rc.cb, PASS_NAME);

                auto* gpuSceneDatabase = rc.view().gpuSceneDatabase;
                auto* gpuSceneView     = rc.view().gpuSceneView;
                if (!gpuSceneDatabase || !gpuSceneView || !gpuSceneDatabase->resources)
                    return;
                if (!gpuSceneDatabase->instanceBuffer || !gpuSceneDatabase->meshTableBuffer ||
                    !gpuSceneDatabase->transformBuffer || !gpuSceneDatabase->resources->meshlets.meshletsBuffer ||
                    !gpuSceneView->visibleMeshletBuffer || !gpuSceneView->visibleMeshletCountBuffer)
                    return;

                auto* cameraUbo = resources.get<framegraph::FrameGraphBuffer>(pd.camera).buffer;
                if (!cameraUbo)
                    return;

                auto variantHash =
                    getShaderLib().computeVariantHash("meshlet_cull.comp", vshadersystem::ShaderStage::eComp, {});
                const auto* pipeline = getPipeline(variantHash);
                if (!pipeline)
                    return;

                uint32_t zero = 0;
                rc.rd.uploadS(*gpuSceneView->visibleMeshletCountBuffer, 0, sizeof(uint32_t), &zero);

                rhi::prepareForComputing(rc.cb, *gpuSceneDatabase->instanceBuffer);
                rhi::prepareForComputing(rc.cb, *gpuSceneDatabase->meshTableBuffer);
                rhi::prepareForComputing(rc.cb, *gpuSceneDatabase->transformBuffer);
                rhi::prepareForComputing(rc.cb, *gpuSceneDatabase->resources->meshlets.meshletsBuffer);
                rhi::prepareForComputing(rc.cb, *gpuSceneView->visibleMeshletBuffer);
                rhi::prepareForComputing(rc.cb, *gpuSceneView->visibleMeshletCountBuffer);

                rc.resourceSet[0] = {
                    {0, rhi::bindings::UniformBuffer {.buffer = cameraUbo}},
                    {2, rhi::bindings::StorageBuffer {.buffer = gpuSceneDatabase->instanceBuffer.get()}},
                    {3, rhi::bindings::StorageBuffer {.buffer = gpuSceneDatabase->meshTableBuffer.get()}},
                    {4,
                     rhi::bindings::StorageBuffer {.buffer =
                                                       gpuSceneDatabase->resources->meshlets.meshletsBuffer.get()}},
                    {5, rhi::bindings::StorageBuffer {.buffer = gpuSceneDatabase->transformBuffer.get()}},
                    {6, rhi::bindings::StorageBuffer {.buffer = gpuSceneView->visibleMeshletBuffer.get()}},
                    {7, rhi::bindings::StorageBuffer {.buffer = gpuSceneView->visibleMeshletCountBuffer.get()}},
                };

                CullPushConstants pc {};
                pc.instanceCount      = instanceCount;
                pc.maxVisibleMeshlets = maxVisible;
                pc.enableConeCull     = 0u;

                rc.cb.bindPipeline(*pipeline);
                rc.bindDescriptorSets(*pipeline);
                rc.cb.pushConstants(rhi::ShaderStages::eCompute, 0, &pc);
                rc.cb.dispatch({(instanceCount + 63u) / 64u, 1u, 1u});
                rc.clear();
            });

        ctx.data.set(kResKey_MeshletCullDone, data.token);
        return data.token;
    }

    rhi::ComputePipeline MeshletCullPass::createPipeline(uint64_t variantHash) const
    {
        auto shader = getShaderLib().load(variantHash, vshadersystem::ShaderStage::eComp);
        if (!shader)
        {
            VULTRA_CORE_ERROR("[MeshletCullPass] Failed to load compute shader variant");
            return {};
        }
        return getRenderDevice().createComputePipelineBuiltin(shader->spirv);
    }
} // namespace vultra
