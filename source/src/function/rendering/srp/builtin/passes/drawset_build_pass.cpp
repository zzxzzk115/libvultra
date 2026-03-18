
#include "vultra/function/rendering/srp/builtin/passes/drawset_build_pass.hpp"
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
        constexpr auto PASS_NAME = "DrawsetBuildPass";

        constexpr uint32_t kRenderQueueCount = 8u;

        struct DrawsetBuildPushConstants
        {
            uint32_t maxDraws {0};
            uint32_t useIndirectCount {0};
            uint32_t padding1 {0};
            uint32_t padding2 {0};
        };
    } // namespace

    void DrawsetBuildPass::addPass(FrameGraphBuildContext& ctx)
    {
        // Get persistent resources from ctx.data before PassData
        auto drawBuffer     = ctx.data.get(kResKey_DrawBuffer);
        auto meshletsBuffer = ctx.data.get(kResKey_MeshletsBuffer);
        auto indirectBuffer = ctx.data.tryGet(kResKey_IndirectBuffer);
        auto drawSetBuffer  = ctx.data.tryGet(kResKey_DrawSetBuffer);

        struct PassData
        {
            FrameGraphResource drawBuffer;
            FrameGraphResource meshletsBuffer;
            FrameGraphResource indirectBuffer;
            FrameGraphResource drawSetBuffer;
        };

        auto*      gpuSceneView = ctx.view().gpuSceneView;
        const auto maxDraws     = gpuSceneView ? gpuSceneView->maxDraws : 0u;

        auto data = ctx.fg.addCallbackPass<PassData>(
            PASS_NAME,
            [drawBuffer, meshletsBuffer, indirectBuffer, drawSetBuffer, maxDraws](FrameGraph::Builder& builder,
                                                                                  PassData&            pd) {
                PASS_SETUP_ZONE;

                if (drawBuffer)
                {
                    pd.drawBuffer = builder.read(drawBuffer,
                                                 framegraph::BindingInfo {
                                                     .location      = {.set = 0, .binding = 1},
                                                     .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                                 });
                }
                if (meshletsBuffer)
                {
                    pd.meshletsBuffer = builder.read(meshletsBuffer,
                                                     framegraph::BindingInfo {
                                                         .location      = {.set = 0, .binding = 4},
                                                         .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                                     });
                }
                pd.indirectBuffer = indirectBuffer ?
                                        indirectBuffer :
                                        builder.create<framegraph::FrameGraphBuffer>(
                                            "DrawIndirectBuffer",
                                            {
                                                .type     = framegraph::BufferType::eDrawIndirectBuffer,
                                                .stride   = sizeof(rhi::DrawIndirectCommand),
                                                .capacity = std::max<uint32_t>(1u, maxDraws * kRenderQueueCount),
                                                .drawIndirectType = rhi::DrawIndirectType::eNonIndexed,
                                            });
                pd.indirectBuffer = builder.write(pd.indirectBuffer,
                                                  framegraph::BindingInfo {
                                                      .location      = {.set = 0, .binding = 12},
                                                      .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                                  });
                pd.drawSetBuffer  = drawSetBuffer ? drawSetBuffer :
                                                    builder.create<framegraph::FrameGraphBuffer>(
                                                       "DrawSetBuffer",
                                                       {
                                                            .type       = framegraph::BufferType::eStorageBuffer,
                                                            .stride     = sizeof(uint32_t),
                                                            .capacity   = kRenderQueueCount,
                                                            .extraUsage = vk::BufferUsageFlagBits::eIndirectBuffer,
                                                       });
                pd.drawSetBuffer  = builder.write(pd.drawSetBuffer,
                                                 framegraph::BindingInfo {
                                                      .location      = {.set = 0, .binding = 30},
                                                      .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                                 });
            },
            [this](const PassData& /*pd*/, FrameGraphPassResources& /*resources*/, void* ctxPtr) {
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

                auto variantHash =
                    getShaderLib().computeVariantHash("drawset_build.comp", vshadersystem::ShaderStage::eComp, {});
                const auto* pipeline = getPipeline(variantHash);
                if (!pipeline)
                    return;

                DrawsetBuildPushConstants pc {};
                pc.maxDraws = gpuSceneView->maxDraws;
                pc.useIndirectCount =
                    HasFlagValues(rc.rd.getFeatureReport().flags,
                                  vultra::rhi::RenderDeviceFeatureReportFlagBits::eDrawIndirectCount) ?
                        1u :
                        0u;

                rc.cb.bindPipeline(*pipeline);
                rc.bindDescriptorSets(*pipeline);
                rc.cb.pushConstants(rhi::ShaderStages::eCompute, 0, &pc);
                rc.cb.dispatch({1u, 1u, 1u});
                rc.clear();
            });

        ctx.data.set(kResKey_DrawBuffer, data.drawBuffer);
        ctx.data.set(kResKey_MeshletsBuffer, data.meshletsBuffer);
        ctx.data.set(kResKey_IndirectBuffer, data.indirectBuffer);
        ctx.data.set(kResKey_DrawSetBuffer, data.drawSetBuffer);
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
