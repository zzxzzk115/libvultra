#include "vultra/function/rendering/srp/builtin/passes/meshlet_cull_pass.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/function/framegraph/framegraph_buffer.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/rendering/srp/builtin/resource_keys.hpp"
#include "vultra/function/resource/gpu_visible_meshlet.hpp"

#include <FrameGraphResource.hpp>
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

    void MeshletCullPass::addPass(FrameGraphBuildContext& ctx)
    {
        auto instanceBuffer             = ctx.data.tryGet(kResKey_InstanceBuffer);
        auto meshTableBuffer            = ctx.data.tryGet(kResKey_MeshTableBuffer);
        auto meshletsBuffer             = ctx.data.tryGet(kResKey_MeshletsBuffer);
        auto transformBuffer            = ctx.data.tryGet(kResKey_TransformBuffer);
        auto visibleInstanceBuffer      = ctx.data.get(kResKey_VisibleInstanceBuffer);
        auto visibleInstanceCountBuffer = ctx.data.get(kResKey_VisibleInstanceCountBuffer);
        auto meshletCullDispatchArgsBuf = ctx.data.get(kResKey_MeshletCullDispatchArgsBuffer);

        struct ResetPassData
        {
            FrameGraphResource visibleMeshletBuffer;
            FrameGraphResource visibleMeshletCountBuffer;
        };

        struct PassData
        {
            FrameGraphResource camera;
            FrameGraphResource visibleMeshletBuffer;
            FrameGraphResource visibleMeshletCountBuffer;
            FrameGraphResource meshletCullDispatchArgsBuffer;
        };

        const auto cameraBlock = ctx.bb.get<CameraData>().cameraBlock.fgResource;

        auto*      gpuSceneView        = ctx.view().gpuSceneView;
        const auto maxVisibleInstances = gpuSceneView ? gpuSceneView->maxVisibleInstances : 0u;
        const auto maxVisible          = gpuSceneView ? gpuSceneView->maxVisibleMeshlets : 0u;

        auto resetData = ctx.fg.addCallbackPass<ResetPassData>(
            "ResetMeshletCullBuffersPass",
            [maxVisible](FrameGraph::Builder& builder, ResetPassData& pd) {
                PASS_SETUP_ZONE;

                pd.visibleMeshletBuffer =
                    builder.create<framegraph::FrameGraphBuffer>("VisibleMeshletBuffer",
                                                                 {
                                                                     .type     = framegraph::BufferType::eStorageBuffer,
                                                                     .stride   = sizeof(resource::GpuVisibleMeshlet),
                                                                     .capacity = std::max<uint32_t>(1u, maxVisible),
                                                                 });
                pd.visibleMeshletBuffer = builder.write(pd.visibleMeshletBuffer,
                                                        framegraph::BindingInfo {
                                                            .location      = {.set = 0, .binding = 6},
                                                            .pipelineStage = framegraph::PipelineStage::eTransfer,
                                                        });

                pd.visibleMeshletCountBuffer =
                    builder.create<framegraph::FrameGraphBuffer>("VisibleMeshletCountBuffer",
                                                                 {
                                                                     .type     = framegraph::BufferType::eStorageBuffer,
                                                                     .stride   = sizeof(uint32_t),
                                                                     .capacity = 1,
                                                                 });
                pd.visibleMeshletCountBuffer = builder.write(pd.visibleMeshletCountBuffer,
                                                             framegraph::BindingInfo {
                                                                 .location      = {.set = 0, .binding = 7},
                                                                 .pipelineStage = framegraph::PipelineStage::eTransfer,
                                                             });
            },
            [](const ResetPassData& pd, FrameGraphPassResources& resources, void* ctxPtr) {
                auto& rc = *static_cast<FrameGraphExecContext*>(ctxPtr);
                RHI_GPU_ZONE(rc.cb, "ResetMeshletCullBuffersPass");

                // Usually clearing the count is enough, but keep the full clear for now
                // to stay consistent with the current debugging-friendly behavior.
                auto* visibleMeshletBuf = resources.get<framegraph::FrameGraphBuffer>(pd.visibleMeshletBuffer).buffer;
                rc.cb.clear(*visibleMeshletBuf, 0u);

                auto* visibleMeshletCountBuf =
                    resources.get<framegraph::FrameGraphBuffer>(pd.visibleMeshletCountBuffer).buffer;
                rc.cb.clear(*visibleMeshletCountBuf, 0u);

                rc.clear();
            });

        auto data = ctx.fg.addCallbackPass<PassData>(
            PASS_NAME,
            [cameraBlock,
             instanceBuffer,
             meshTableBuffer,
             meshletsBuffer,
             transformBuffer,
             visibleInstanceBuffer,
             visibleInstanceCountBuffer,
             meshletCullDispatchArgsBuf,
             resetData](FrameGraph::Builder& builder, PassData& pd) {
                PASS_SETUP_ZONE;

                pd.camera = builder.read(cameraBlock,
                                         framegraph::BindingInfo {
                                             .location      = {.set = 0, .binding = 0},
                                             .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                         });

                if (instanceBuffer)
                {
                    builder.read(instanceBuffer,
                                 framegraph::BindingInfo {
                                     .location      = {.set = 0, .binding = 2},
                                     .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                 });
                }

                if (meshTableBuffer)
                {
                    builder.read(meshTableBuffer,
                                 framegraph::BindingInfo {
                                     .location      = {.set = 0, .binding = 3},
                                     .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                 });
                }

                if (meshletsBuffer)
                {
                    builder.read(meshletsBuffer,
                                 framegraph::BindingInfo {
                                     .location      = {.set = 0, .binding = 4},
                                     .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                 });
                }

                if (transformBuffer)
                {
                    builder.read(transformBuffer,
                                 framegraph::BindingInfo {
                                     .location      = {.set = 0, .binding = 5},
                                     .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                 });
                }

                if (visibleInstanceBuffer)
                {
                    builder.read(visibleInstanceBuffer,
                                 framegraph::BindingInfo {
                                     .location      = {.set = 0, .binding = 24},
                                     .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                 });
                }

                if (visibleInstanceCountBuffer)
                {
                    builder.read(visibleInstanceCountBuffer,
                                 framegraph::BindingInfo {
                                     .location      = {.set = 0, .binding = 25},
                                     .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                 });
                }

                if (meshletCullDispatchArgsBuf)
                {
                    pd.meshletCullDispatchArgsBuffer =
                        builder.read(meshletCullDispatchArgsBuf,
                                     framegraph::BindingInfo {
                                         .location      = {},
                                         .pipelineStage = framegraph::PipelineStage::eDrawIndirect,
                                     });
                }

                pd.visibleMeshletBuffer = resetData.visibleMeshletBuffer;
                pd.visibleMeshletBuffer = builder.write(pd.visibleMeshletBuffer,
                                                        framegraph::BindingInfo {
                                                            .location      = {.set = 0, .binding = 6},
                                                            .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                                        });

                pd.visibleMeshletCountBuffer = resetData.visibleMeshletCountBuffer;
                pd.visibleMeshletCountBuffer =
                    builder.write(pd.visibleMeshletCountBuffer,
                                  framegraph::BindingInfo {
                                      .location      = {.set = 0, .binding = 7},
                                      .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                  });
            },
            [this, maxVisibleInstances, maxVisible](
                const PassData& pd, FrameGraphPassResources& resources, void* ctxPtr) {
                auto& rc = *static_cast<FrameGraphExecContext*>(ctxPtr);
                setRenderDevice(rc.rd);
                if (!rc.ext.builtinShaderLib)
                    return;
                setShaderLib(*rc.ext.builtinShaderLib);

                RHI_GPU_ZONE(rc.cb, PASS_NAME);

                if (maxVisibleInstances == 0 || maxVisible == 0)
                {
                    rc.clear();
                    return;
                }

                CullPushConstants pc {};
                pc.instanceCount      = maxVisibleInstances;
                pc.maxVisibleMeshlets = maxVisible;
                pc.enableConeCull     = 0u;

                auto variantHash =
                    getShaderLib().computeVariantHash("meshlet_cull.comp", vshadersystem::ShaderStage::eComp, {});
                const auto* pipeline = getPipeline(variantHash);
                if (!pipeline)
                    return;

                rc.cb.bindPipeline(*pipeline);
                rc.bindDescriptorSets(*pipeline);
                rc.cb.pushConstants(rhi::ShaderStages::eCompute, 0, &pc);

                auto* meshletCullDispatchArgsBuffer =
                    resources.get<framegraph::FrameGraphBuffer>(pd.meshletCullDispatchArgsBuffer).buffer;
                rhi::prepareForDrawingIndirect(rc.cb, *meshletCullDispatchArgsBuffer);
                rc.cb.dispatchIndirect(*meshletCullDispatchArgsBuffer);

                rc.clear();
            });

        ctx.data.set(kResKey_VisibleMeshletBuffer, data.visibleMeshletBuffer);
        ctx.data.set(kResKey_VisibleMeshletCountBuffer, data.visibleMeshletCountBuffer);
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