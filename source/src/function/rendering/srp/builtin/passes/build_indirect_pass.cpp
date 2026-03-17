#include "vultra/function/rendering/srp/builtin/passes/build_indirect_pass.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/function/framegraph/framegraph_buffer.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"

#include <fg/FrameGraph.hpp>

namespace vultra
{
    namespace
    {
        constexpr auto PASS_NAME = "BuildIndirectPass";

        struct BuildPushConstants
        {
            uint32_t maxDraws {0};
            uint32_t vertexAddressLo {0};
            uint32_t vertexAddressHi {0};
            uint32_t padding0 {0};
        };
    } // namespace

    FrameGraphResource BuildIndirectPass::addPass(FrameGraphBuildContext& ctx, FrameGraphResource cullToken)
    {
        struct PassData
        {
            FrameGraphResource cullToken;
            FrameGraphResource token;
        };

        auto data = ctx.fg.addCallbackPass<PassData>(
            PASS_NAME,
            [cullToken](FrameGraph::Builder& builder, PassData& pd) {
                PASS_SETUP_ZONE;

                pd.cullToken = cullToken;
                if (pd.cullToken)
                {
                    pd.cullToken = builder.read(pd.cullToken,
                                                framegraph::BindingInfo {
                                                    .location      = {.set = 0, .binding = 31},
                                                    .pipelineStage = framegraph::PipelineStage::eTransfer,
                                                });
                }

                pd.token =
                    builder.create<framegraph::FrameGraphBuffer>("BuildIndirectToken",
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
                if (!gpuSceneView->visibleMeshletBuffer || !gpuSceneView->visibleMeshletCountBuffer ||
                    !gpuSceneView->drawBuffer || !gpuSceneDatabase->instanceBuffer ||
                    !gpuSceneDatabase->meshTableBuffer ||
                    !gpuSceneDatabase->transformBuffer || !gpuSceneDatabase->resources->meshlets.meshletsBuffer ||
                    !gpuSceneDatabase->resources->materialTableBuffer)
                    return;

                auto variantHash =
                    getShaderLib().computeVariantHash("build_indirect.comp", vshadersystem::ShaderStage::eComp, {});
                const auto* pipeline = getPipeline(variantHash);
                if (!pipeline)
                    return;

                rhi::prepareForComputing(rc.cb, *gpuSceneView->visibleMeshletBuffer);
                rhi::prepareForComputing(rc.cb, *gpuSceneView->visibleMeshletCountBuffer);
                rhi::prepareForComputing(rc.cb, *gpuSceneDatabase->instanceBuffer);
                rhi::prepareForComputing(rc.cb, *gpuSceneDatabase->meshTableBuffer);
                rhi::prepareForComputing(rc.cb, *gpuSceneDatabase->transformBuffer);
                rhi::prepareForComputing(rc.cb, *gpuSceneDatabase->resources->meshlets.meshletsBuffer);
                rhi::prepareForComputing(rc.cb, *gpuSceneDatabase->resources->materialTableBuffer);
                rhi::prepareForComputing(rc.cb, *gpuSceneView->drawBuffer);

                rc.resourceSet[0] = {
                    {1, rhi::bindings::StorageBuffer {.buffer = gpuSceneView->drawBuffer.get()}},
                    {2, rhi::bindings::StorageBuffer {.buffer = gpuSceneDatabase->instanceBuffer.get()}},
                    {3, rhi::bindings::StorageBuffer {.buffer = gpuSceneDatabase->meshTableBuffer.get()}},
                    {4,
                     rhi::bindings::StorageBuffer {.buffer =
                                                       gpuSceneDatabase->resources->meshlets.meshletsBuffer.get()}},
                    {5, rhi::bindings::StorageBuffer {.buffer = gpuSceneDatabase->transformBuffer.get()}},
                    {6, rhi::bindings::StorageBuffer {.buffer = gpuSceneView->visibleMeshletBuffer.get()}},
                    {7, rhi::bindings::StorageBuffer {.buffer = gpuSceneView->visibleMeshletCountBuffer.get()}},
                    {8,
                     rhi::bindings::StorageBuffer {.buffer = gpuSceneDatabase->resources->materialTableBuffer.get()}},
                };

                const uint64_t vertexAddress = gpuSceneDatabase->resources->geometry.vertexBytesAddress;

                BuildPushConstants pc {};
                pc.maxDraws        = gpuSceneView->maxDraws;
                pc.vertexAddressLo = static_cast<uint32_t>(vertexAddress & 0xffffffffull);
                pc.vertexAddressHi = static_cast<uint32_t>((vertexAddress >> 32u) & 0xffffffffull);

                rc.cb.bindPipeline(*pipeline);
                rc.bindDescriptorSets(*pipeline);
                rc.cb.pushConstants(rhi::ShaderStages::eCompute, 0, &pc);
                rc.cb.dispatch({(gpuSceneView->maxDraws + 63u) / 64u, 1u, 1u});
                rc.clear();
            });

        return data.token;
    }

    rhi::ComputePipeline BuildIndirectPass::createPipeline(uint64_t variantHash) const
    {
        auto shader = getShaderLib().load(variantHash, vshadersystem::ShaderStage::eComp);
        if (!shader)
        {
            VULTRA_CORE_ERROR("[BuildIndirectPass] Failed to load compute shader variant");
            return {};
        }
        return getRenderDevice().createComputePipelineBuiltin(shader->spirv);
    }
} // namespace vultra
