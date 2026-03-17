#include "vultra/function/rendering/srp/builtin/passes/coarse_instance_cull_pass.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/function/framegraph/framegraph_buffer.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"

#include <fg/FrameGraph.hpp>

namespace vultra
{
    namespace
    {
        constexpr auto PASS_NAME = "CoarseInstanceCullPass";

        struct CoarseCullPushConstants
        {
            uint32_t instanceCount {0};
            uint32_t maxVisibleInstances {0};
            uint32_t padding0 {0};
            uint32_t padding1 {0};
        };
    } // namespace

    FrameGraphResource CoarseInstanceCullPass::addPass(FrameGraphBuildContext& ctx)
    {
        struct PassData
        {
            FrameGraphResource camera;
            FrameGraphResource token;
        };

        const auto cameraBlock = ctx.bb.get<CameraData>().cameraBlock.fgResource;

        auto*      gpuSceneDatabase   = ctx.view().gpuSceneDatabase;
        auto*      gpuSceneView       = ctx.view().gpuSceneView;
        const auto instanceCount      = gpuSceneDatabase ? static_cast<uint32_t>(gpuSceneDatabase->instances.size()) : 0u;
        const auto maxVisibleInstance = gpuSceneView ? gpuSceneView->maxVisibleInstances : 0u;

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
                    builder.create<framegraph::FrameGraphBuffer>("CoarseInstanceCullToken",
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
            [this, instanceCount, maxVisibleInstance](const PassData& pd, FrameGraphPassResources& resources, void* ctxPtr) {
                auto& rc = *static_cast<FrameGraphExecContext*>(ctxPtr);
                setRenderDevice(rc.rd);
                if (!rc.ext.builtinShaderLib)
                    return;
                setShaderLib(*rc.ext.builtinShaderLib);

                RHI_GPU_ZONE(rc.cb, PASS_NAME);

                if (instanceCount == 0 || maxVisibleInstance == 0)
                {
                    rc.clear();
                    return;
                }

                auto* gpuSceneDatabase = rc.view().gpuSceneDatabase;
                auto* gpuSceneView     = rc.view().gpuSceneView;
                if (!gpuSceneDatabase || !gpuSceneView)
                    return;

                gpuSceneView->ensureVisibleInstanceBuffers(rc.rd);

                if (!gpuSceneDatabase->instanceBuffer || !gpuSceneDatabase->meshTableBuffer || !gpuSceneDatabase->transformBuffer ||
                    !gpuSceneView->visibleInstanceBuffer || !gpuSceneView->visibleInstanceCountBuffer ||
                    !gpuSceneView->meshletCullDispatchArgsBuffer)
                    return;

                auto* cameraUbo = resources.get<framegraph::FrameGraphBuffer>(pd.camera).buffer;
                if (!cameraUbo)
                    return;

                auto variantHash =
                    getShaderLib().computeVariantHash("coarse_instance_cull.comp", vshadersystem::ShaderStage::eComp, {});
                const auto* pipeline = getPipeline(variantHash);
                if (!pipeline)
                    return;

                // Per-frame tiny resets must be recorded in-frame, not uploaded via synchronous uploadS.
                rc.cb.clear(*gpuSceneView->visibleInstanceCountBuffer, 0u);
                struct DispatchArgsInit
                {
                    uint32_t x;
                    uint32_t y;
                    uint32_t z;
                } argsInit {0u, 1u, 1u};
                rc.cb.update(*gpuSceneView->meshletCullDispatchArgsBuffer, 0, sizeof(DispatchArgsInit), &argsInit);

                rhi::prepareForComputing(rc.cb, *gpuSceneDatabase->instanceBuffer);
                rhi::prepareForComputing(rc.cb, *gpuSceneDatabase->meshTableBuffer);
                rhi::prepareForComputing(rc.cb, *gpuSceneDatabase->transformBuffer);
                rhi::prepareForComputing(rc.cb, *gpuSceneView->visibleInstanceBuffer);
                rhi::prepareForComputing(rc.cb, *gpuSceneView->visibleInstanceCountBuffer);
                rhi::prepareForComputing(rc.cb, *gpuSceneView->meshletCullDispatchArgsBuffer);

                rc.resourceSet[0] = {
                    {0, rhi::bindings::UniformBuffer {.buffer = cameraUbo}},
                    {2, rhi::bindings::StorageBuffer {.buffer = gpuSceneDatabase->instanceBuffer.get()}},
                    {3, rhi::bindings::StorageBuffer {.buffer = gpuSceneDatabase->meshTableBuffer.get()}},
                    {5, rhi::bindings::StorageBuffer {.buffer = gpuSceneDatabase->transformBuffer.get()}},
                    {24, rhi::bindings::StorageBuffer {.buffer = gpuSceneView->visibleInstanceBuffer.get()}},
                    {25, rhi::bindings::StorageBuffer {.buffer = gpuSceneView->visibleInstanceCountBuffer.get()}},
                    {26, rhi::bindings::StorageBuffer {.buffer = gpuSceneView->meshletCullDispatchArgsBuffer.get()}},
                };

                CoarseCullPushConstants pc {};
                pc.instanceCount        = instanceCount;
                pc.maxVisibleInstances  = maxVisibleInstance;

                rc.cb.bindPipeline(*pipeline);
                rc.bindDescriptorSets(*pipeline);
                rc.cb.pushConstants(rhi::ShaderStages::eCompute, 0, &pc);
                rc.cb.dispatch({(instanceCount + 63u) / 64u, 1u, 1u});
                rc.clear();
            });

        return data.token;
    }

    rhi::ComputePipeline CoarseInstanceCullPass::createPipeline(uint64_t variantHash) const
    {
        auto shader = getShaderLib().load(variantHash, vshadersystem::ShaderStage::eComp);
        if (!shader)
        {
            VULTRA_CORE_ERROR("[CoarseInstanceCullPass] Failed to load compute shader variant");
            return {};
        }
        return getRenderDevice().createComputePipelineBuiltin(shader->spirv);
    }
} // namespace vultra
