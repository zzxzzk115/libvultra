#include "vultra/function/rendering/srp/builtin/passes/build_indirect_pass.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/function/framegraph/framegraph_buffer.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/rendering/srp/builtin/resource_keys.hpp"
#include "vultra/function/resource/gpu_draw.hpp"

#include <fg/FrameGraph.hpp>
#include <fg/FrameGraphResource.hpp>

namespace vultra
{
    BuildIndirectPass::BuildIndirectPass() { setShaderProfile(rhi::ShaderProfile::eHighend); }

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

    void BuildIndirectPass::addPass(FrameGraphBuildContext& ctx)
    {
        auto drawBuffer                = ctx.data.tryGet(kResKey_DrawBuffer);
        auto instanceBuffer            = ctx.data.get(kResKey_InstanceBuffer);
        auto meshTableBuffer           = ctx.data.get(kResKey_MeshTableBuffer);
        auto transformBuffer           = ctx.data.get(kResKey_TransformBuffer);
        auto meshletsBuffer            = ctx.data.get(kResKey_MeshletsBuffer);
        auto visibleMeshletBuffer      = ctx.data.get(kResKey_VisibleMeshletBuffer);
        auto visibleMeshletCountBuffer = ctx.data.get(kResKey_VisibleMeshletCountBuffer);
        auto materialTableBuffer       = ctx.data.get(kResKey_MaterialTableBuffer);

        struct PassData
        {
            FrameGraphResource drawBuffer;
            FrameGraphResource instanceBuffer;
            FrameGraphResource meshTableBuffer;
            FrameGraphResource transformBuffer;
            FrameGraphResource meshletsBuffer;
            FrameGraphResource visibleMeshletBuffer;
            FrameGraphResource visibleMeshletCountBuffer;
            FrameGraphResource materialTableBuffer;
        };

        auto*      gpuSceneView = ctx.view().gpuSceneView;
        const auto maxDraws     = gpuSceneView ? gpuSceneView->maxDraws : 0u;

        auto data = ctx.fg.addCallbackPass<PassData>(
            PASS_NAME,
            [drawBuffer,
             instanceBuffer,
             meshTableBuffer,
             transformBuffer,
             meshletsBuffer,
             visibleMeshletBuffer,
             visibleMeshletCountBuffer,
             materialTableBuffer,
             maxDraws](FrameGraph::Builder& builder, PassData& pd) {
                PASS_SETUP_ZONE;

                pd.drawBuffer =
                    drawBuffer ?
                        drawBuffer :
                        builder.create<framegraph::FrameGraphBuffer>("DrawBuffer",
                                                                     {
                                                                         .type = framegraph::BufferType::eStorageBuffer,
                                                                         .stride   = sizeof(resource::GpuDrawRecord),
                                                                         .capacity = std::max<uint32_t>(1u, maxDraws),
                                                                     });
                pd.drawBuffer = builder.write(pd.drawBuffer,
                                              framegraph::BindingInfo {
                                                  .location      = {.set = 0, .binding = 1},
                                                  .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                              });

                if (instanceBuffer)
                {
                    pd.instanceBuffer = builder.read(instanceBuffer,
                                                     framegraph::BindingInfo {
                                                         .location      = {.set = 0, .binding = 2},
                                                         .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                                     });
                }

                if (meshTableBuffer)
                {
                    pd.meshTableBuffer = builder.read(meshTableBuffer,
                                                      framegraph::BindingInfo {
                                                          .location      = {.set = 0, .binding = 3},
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

                if (transformBuffer)
                {
                    pd.transformBuffer = builder.read(transformBuffer,
                                                      framegraph::BindingInfo {
                                                          .location      = {.set = 0, .binding = 5},
                                                          .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                                      });
                }

                if (visibleMeshletBuffer)
                {
                    pd.visibleMeshletBuffer =
                        builder.read(visibleMeshletBuffer,
                                     framegraph::BindingInfo {
                                         .location      = {.set = 0, .binding = 6},
                                         .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                     });
                }

                if (visibleMeshletCountBuffer)
                {
                    pd.visibleMeshletCountBuffer =
                        builder.read(visibleMeshletCountBuffer,
                                     framegraph::BindingInfo {
                                         .location      = {.set = 0, .binding = 7},
                                         .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                     });
                }

                if (materialTableBuffer)
                {
                    pd.materialTableBuffer =
                        builder.read(materialTableBuffer,
                                     framegraph::BindingInfo {
                                         .location      = {.set = 0, .binding = 8},
                                         .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                     });
                }
            },
            [this](const PassData&, FrameGraphPassResources&, void* ctxPtr) {
                VULTRA_SCOPED_FRAMEGRAPH_EXEC_CONTEXT(rc, ctxPtr);
                setRenderDevice(rc.rd);
                if (!rc.ext.builtinShaderLib)
                {
                    return;
                }
                setShaderLib(*rc.ext.builtinShaderLibForProfile(getShaderProfile()));

                RHI_GPU_ZONE(rc.cb, PASS_NAME);

                auto* gpuSceneDatabase = rc.view().gpuSceneDatabase;
                auto* gpuSceneView     = rc.view().gpuSceneView;
                if (!gpuSceneDatabase || !gpuSceneView || !gpuSceneDatabase->resources)
                {
                    return;
                }
                if (gpuSceneView->maxDraws == 0)
                {
                    return;
                }

                auto variantHash =
                    computeHighendVariantHash("build_indirect.comp", vshadersystem::ShaderStage::eComp, {});
                const auto* pipeline = getPipeline(variantHash);
                if (!pipeline)
                {
                    return;
                }

                const auto vertexAddress = gpuSceneDatabase->resources->geometry.vertexBytesAddress.value;

                BuildPushConstants pc {};
                pc.maxDraws        = gpuSceneView->maxDraws;
                pc.vertexAddressLo = static_cast<uint32_t>(vertexAddress & 0xffffffffull);
                pc.vertexAddressHi = static_cast<uint32_t>((vertexAddress >> 32u) & 0xffffffffull);

                rc.cb.bindPipeline(*pipeline);
                rc.bindDescriptorSets(*pipeline);
                rc.cb.pushConstants(rhi::ShaderStages::eCompute, 0, &pc);
                rc.cb.dispatch({(gpuSceneView->maxDraws + 63u) / 64u, 1u, 1u});
            });

        ctx.data.set(kResKey_DrawBuffer, data.drawBuffer);
        ctx.data.set(kResKey_InstanceBuffer, data.instanceBuffer);
        ctx.data.set(kResKey_MeshTableBuffer, data.meshTableBuffer);
        ctx.data.set(kResKey_TransformBuffer, data.transformBuffer);
        ctx.data.set(kResKey_MeshletsBuffer, data.meshletsBuffer);
        ctx.data.set(kResKey_VisibleMeshletBuffer, data.visibleMeshletBuffer);
        ctx.data.set(kResKey_VisibleMeshletCountBuffer, data.visibleMeshletCountBuffer);
        ctx.data.set(kResKey_MaterialTableBuffer, data.materialTableBuffer);
    }

    rhi::ComputePipeline BuildIndirectPass::createPipeline(uint64_t variantHash) const
    {
        auto shader = loadHighendShaderVariant(variantHash, vshadersystem::ShaderStage::eComp);
        if (!shader)
        {
            VULTRA_CORE_ERROR("[BuildIndirectPass] Failed to load compute shader variant");
            return {};
        }
        return getRenderDevice().createComputePipelineBuiltin(shader->spirv);
    }
} // namespace vultra
