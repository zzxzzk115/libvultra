#include "vultra/function/rendering/srp/builtin/passes/drawset_build_pass.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/function/framegraph/framegraph_buffer.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"

#include <fg/FrameGraph.hpp>

namespace vultra
{
    namespace
    {
        constexpr auto PASS_NAME = "DrawsetBuildPass";

        struct DrawsetBuildPushConstants
        {
            uint32_t maxDraws {0};
            uint32_t padding0 {0};
            uint32_t padding1 {0};
            uint32_t padding2 {0};
        };
    } // namespace

    FrameGraphResource DrawsetBuildPass::addPass(FrameGraphBuildContext& ctx, FrameGraphResource buildToken)
    {
        struct PassData
        {
            FrameGraphResource buildToken;
            FrameGraphResource token;
        };

        auto data = ctx.fg.addCallbackPass<PassData>(
            PASS_NAME,
            [buildToken](FrameGraph::Builder& builder, PassData& pd) {
                PASS_SETUP_ZONE;

                pd.buildToken = buildToken;
                if (pd.buildToken)
                {
                    pd.buildToken = builder.read(pd.buildToken,
                                                 framegraph::BindingInfo {
                                                     .location      = {.set = 0, .binding = 31},
                                                     .pipelineStage = framegraph::PipelineStage::eTransfer,
                                                 });
                }

                pd.token = builder.create<framegraph::FrameGraphBuffer>("DrawsetBuildToken",
                                                                        {
                                                                            .type =
                                                                                framegraph::BufferType::eStorageBuffer,
                                                                            .stride = sizeof(uint32_t),
                                                                            .capacity = 1,
                                                                        });
                pd.token = builder.write(pd.token,
                                         framegraph::BindingInfo {
                                             .location      = {.set = 0, .binding = 31},
                                             .pipelineStage = framegraph::PipelineStage::eTransfer,
                                         });
            },
            [this](const PassData&, FrameGraphPassResources&, void* ctxPtr) {
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
                if (gpuSceneView->maxDraws == 0)
                {
                    rc.clear();
                    return;
                }

                gpuSceneView->ensureDrawSetBuffer(rc.rd);

                if (!gpuSceneView->drawBuffer || !gpuSceneView->drawSetBuffer ||
                    !gpuSceneView->indirectBuffer.has_value() ||
                    !gpuSceneDatabase->resources->meshlets.meshletsBuffer)
                    return;

                auto variantHash = getShaderLib().computeVariantHash(
                    "drawset_build.comp", vshadersystem::ShaderStage::eComp, {});
                const auto* pipeline = getPipeline(variantHash);
                if (!pipeline)
                    return;

                rhi::prepareForComputing(rc.cb, *gpuSceneView->drawBuffer);
                rhi::prepareForComputing(rc.cb, *gpuSceneView->drawSetBuffer);
                rhi::prepareForComputing(rc.cb, gpuSceneView->indirectBuffer.value());
                rhi::prepareForComputing(rc.cb, *gpuSceneDatabase->resources->meshlets.meshletsBuffer);

                rc.resourceSet[0] = {
                    {1, rhi::bindings::StorageBuffer {.buffer = gpuSceneView->drawBuffer.get()}},
                    {4,
                     rhi::bindings::StorageBuffer {
                         .buffer = gpuSceneDatabase->resources->meshlets.meshletsBuffer.get()}},
                    {12, rhi::bindings::StorageBuffer {.buffer = &gpuSceneView->indirectBuffer.value()}},
                    {30, rhi::bindings::StorageBuffer {.buffer = gpuSceneView->drawSetBuffer.get()}},
                };

                DrawsetBuildPushConstants pc {};
                pc.maxDraws = gpuSceneView->maxDraws;

                rc.cb.bindPipeline(*pipeline);
                rc.bindDescriptorSets(*pipeline);
                rc.cb.pushConstants(rhi::ShaderStages::eCompute, 0, &pc);
                rc.cb.dispatch({1u, 1u, 1u});
                rc.clear();
            });

        return data.token;
    }

    rhi::ComputePipeline DrawsetBuildPass::createPipeline(uint64_t variantHash) const
    {
        auto shader = getShaderLib().load(variantHash, vshadersystem::ShaderStage::eComp);
        if (!shader)
        {
            VULTRA_CORE_ERROR("[DrawsetBuildPass] Failed to load compute shader variant");
            return {};
        }
        return getRenderDevice().createComputePipelineBuiltin(shader->spirv);
    }
} // namespace vultra
