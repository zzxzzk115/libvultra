#include "vultra/function/rendering/srp/builtin/passes/coarse_instance_cull_pass.hpp"

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
        constexpr auto PASS_NAME = "CoarseInstanceCullPass";

        struct CoarseCullPushConstants
        {
            uint32_t instanceCount {0};
            uint32_t maxVisibleInstances {0};
            uint32_t padding0 {0};
            uint32_t padding1 {0};
        };
    } // namespace

    void CoarseInstanceCullPass::addPass(FrameGraphBuildContext& ctx)
    {
        auto instanceBuffer                = ctx.data.get(kResKey_InstanceBuffer);
        auto meshTableBuffer               = ctx.data.get(kResKey_MeshTableBuffer);
        auto transformBuffer               = ctx.data.get(kResKey_TransformBuffer);
        auto visibleInstanceBuffer         = ctx.data.tryGet(kResKey_VisibleInstanceBuffer);
        auto visibleInstanceCountBuffer    = ctx.data.tryGet(kResKey_VisibleInstanceCountBuffer);
        auto meshletCullDispatchArgsBuffer = ctx.data.tryGet(kResKey_MeshletCullDispatchArgsBuffer);

        struct ResetPassData
        {
            FrameGraphResource visibleInstanceCountBuffer;
            FrameGraphResource meshletCullDispatchArgsBuffer;
        };

        struct PassData
        {
            FrameGraphResource camera;

            FrameGraphResource visibleInstanceBuffer;
            FrameGraphResource visibleInstanceCountBuffer;
            FrameGraphResource meshletCullDispatchArgsBuffer;
        };

        const auto cameraBlock = ctx.bb.get<CameraData>().cameraBlock.fgResource;

        auto*      gpuSceneDatabase = ctx.view().gpuSceneDatabase;
        auto*      gpuSceneView     = ctx.view().gpuSceneView;
        const auto instanceCount    = gpuSceneDatabase ? static_cast<uint32_t>(gpuSceneDatabase->instances.size()) : 0u;
        const auto maxVisibleInstance = gpuSceneView ? gpuSceneView->maxVisibleInstances : 0u;

        auto resetData = ctx.fg.addCallbackPass<ResetPassData>(
            "ResetCoarseCullBuffersPass",
            [maxVisibleInstance, visibleInstanceCountBuffer, meshletCullDispatchArgsBuffer](
                FrameGraph::Builder& builder, ResetPassData& pd) {
                PASS_SETUP_ZONE;

                pd.visibleInstanceCountBuffer =
                    visibleInstanceCountBuffer ?
                        visibleInstanceCountBuffer :
                        builder.create<framegraph::FrameGraphBuffer>("VisibleInstanceCountBuffer",
                                                                     {
                                                                         .type = framegraph::BufferType::eStorageBuffer,
                                                                         .stride   = sizeof(uint32_t),
                                                                         .capacity = 1,
                                                                     });
                pd.visibleInstanceCountBuffer = builder.write(pd.visibleInstanceCountBuffer,
                                                              framegraph::BindingInfo {
                                                                  .location      = {.set = 0, .binding = 25},
                                                                  .pipelineStage = framegraph::PipelineStage::eTransfer,
                                                              });

                pd.meshletCullDispatchArgsBuffer = meshletCullDispatchArgsBuffer ?
                                                       meshletCullDispatchArgsBuffer :
                                                       builder.create<framegraph::FrameGraphBuffer>(
                                                           "MeshletCullDispatchArgsBuffer",
                                                           {
                                                               .type = framegraph::BufferType::eDispatchIndirectBuffer,
                                                               .stride   = sizeof(uint32_t),
                                                               .capacity = 3,
                                                           });
                pd.meshletCullDispatchArgsBuffer =
                    builder.write(pd.meshletCullDispatchArgsBuffer,
                                  framegraph::BindingInfo {
                                      .location      = {.set = 0, .binding = 26},
                                      .pipelineStage = framegraph::PipelineStage::eTransfer,
                                  });
            },
            [](const ResetPassData& pd, FrameGraphPassResources& resources, void* ctxPtr) {
                auto& rc = *static_cast<FrameGraphExecContext*>(ctxPtr);
                RHI_GPU_ZONE(rc.cb, "ResetCoarseCullBuffersPass");

                auto* visibleInstanceCountBuf =
                    resources.get<framegraph::FrameGraphBuffer>(pd.visibleInstanceCountBuffer).buffer;
                auto* meshletCullDispatchArgsBuf =
                    resources.get<framegraph::FrameGraphBuffer>(pd.meshletCullDispatchArgsBuffer).buffer;

                rc.cb.clear(*visibleInstanceCountBuf, 0u);
                struct DispatchArgsInit
                {
                    uint32_t x;
                    uint32_t y;
                    uint32_t z;
                } argsInit {0u, 1u, 1u};
                rc.cb.update(*meshletCullDispatchArgsBuf, 0, sizeof(DispatchArgsInit), &argsInit);
                rc.clear();
            });

        auto data = ctx.fg.addCallbackPass<PassData>(
            PASS_NAME,
            [cameraBlock,
             maxVisibleInstance,
             instanceBuffer,
             meshTableBuffer,
             transformBuffer,
             visibleInstanceBuffer,
             resetData](FrameGraph::Builder& builder, PassData& pd) {
                PASS_SETUP_ZONE;

                pd.camera = builder.read(cameraBlock,
                                         framegraph::BindingInfo {
                                             .location      = {.set = 0, .binding = 0},
                                             .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                         });

                builder.read(instanceBuffer,
                             framegraph::BindingInfo {
                                 .location      = {.set = 0, .binding = 2},
                                 .pipelineStage = framegraph::PipelineStage::eComputeShader,
                             });

                builder.read(meshTableBuffer,
                             framegraph::BindingInfo {
                                 .location      = {.set = 0, .binding = 3},
                                 .pipelineStage = framegraph::PipelineStage::eComputeShader,
                             });

                builder.read(transformBuffer,
                             framegraph::BindingInfo {
                                 .location      = {.set = 0, .binding = 5},
                                 .pipelineStage = framegraph::PipelineStage::eComputeShader,
                             });

                pd.visibleInstanceBuffer = visibleInstanceBuffer ?
                                               visibleInstanceBuffer :
                                               builder.create<framegraph::FrameGraphBuffer>(
                                                   "VisibleInstanceBuffer",
                                                   {
                                                       .type     = framegraph::BufferType::eStorageBuffer,
                                                       .stride   = sizeof(uint32_t),
                                                       .capacity = std::max<uint32_t>(1u, maxVisibleInstance),
                                                   });
                pd.visibleInstanceBuffer = builder.write(pd.visibleInstanceBuffer,
                                                         framegraph::BindingInfo {
                                                             .location      = {.set = 0, .binding = 24},
                                                             .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                                         });

                pd.visibleInstanceCountBuffer =
                    builder.write(resetData.visibleInstanceCountBuffer,
                                  framegraph::BindingInfo {
                                      .location      = {.set = 0, .binding = 25},
                                      .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                  });

                pd.meshletCullDispatchArgsBuffer =
                    builder.write(resetData.meshletCullDispatchArgsBuffer,
                                  framegraph::BindingInfo {
                                      .location      = {.set = 0, .binding = 26},
                                      .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                  });
            },
            [this, instanceCount, maxVisibleInstance](const PassData&, FrameGraphPassResources&, void* ctxPtr) {
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
                if (!gpuSceneDatabase)
                    return;

                auto variantHash = getShaderLib().computeVariantHash(
                    "coarse_instance_cull.comp", vshadersystem::ShaderStage::eComp, {});
                const auto* pipeline = getPipeline(variantHash);
                if (!pipeline)
                    return;

                CoarseCullPushConstants pc {};
                pc.instanceCount       = instanceCount;
                pc.maxVisibleInstances = maxVisibleInstance;

                rc.cb.bindPipeline(*pipeline);
                rc.bindDescriptorSets(*pipeline);
                rc.cb.pushConstants(rhi::ShaderStages::eCompute, 0, &pc);
                rc.cb.dispatch({(instanceCount + 63u) / 64u, 1u, 1u});
                rc.clear();
            });

        ctx.data.set(kResKey_VisibleInstanceBuffer, data.visibleInstanceBuffer);
        ctx.data.set(kResKey_VisibleInstanceCountBuffer, data.visibleInstanceCountBuffer);
        ctx.data.set(kResKey_MeshletCullDispatchArgsBuffer, data.meshletCullDispatchArgsBuffer);
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
