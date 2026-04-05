#include "vultra/function/rendering/srp/builtin/passes/meshlet_hiz_cull_pass.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/function/framegraph/framegraph_import.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"
#include "vultra/function/rendering/srp/builtin/resource_keys.hpp"

#include <fg/FrameGraph.hpp>

namespace vultra
{
    MeshletHiZCullPass::MeshletHiZCullPass() { setShaderProfile(rhi::ShaderProfile::eHighend); }

    namespace
    {
        constexpr auto PASS_NAME = "MeshletHiZCullPass";

        struct HiZCullPushConstants
        {
            uint32_t drawCount {0};
            uint32_t hzbMipCount {0};
            uint32_t enableHiZ {0};
            uint32_t instanceCount {0};
            uint32_t transformCount {0};
            uint32_t meshletCount {0};
            uint32_t padding0 {0};
        };
    } // namespace

    void MeshletHiZCullPass::addPass(FrameGraphBuildContext& ctx)
    {
        struct PassData
        {
            FrameGraphResource camera;
            FrameGraphResource hzb;
            FrameGraphResource instances;
            FrameGraphResource meshTable;
            FrameGraphResource meshlets;
            FrameGraphResource models;
            FrameGraphResource visibleMeshlets;
            FrameGraphResource visibleCount;
        };

        const auto cameraBlock     = ctx.bb.get<CameraData>().cameraBlock.fgResource;
        const auto hzbTexture      = ctx.data.tryGet(kResKey_HzbTexture);
        const auto instanceBuffer  = ctx.data.tryGet(kResKey_InstanceBuffer);
        const auto meshTableBuffer = ctx.data.tryGet(kResKey_MeshTableBuffer);
        const auto meshletBuffer   = ctx.data.tryGet(kResKey_MeshletsBuffer);
        const auto modelBuffer     = ctx.data.tryGet(kResKey_TransformBuffer);
        const auto visibleMeshlets = ctx.data.tryGet(kResKey_VisibleMeshletBuffer);
        const auto visibleCount    = ctx.data.tryGet(kResKey_VisibleMeshletCountBuffer);

        const auto data = ctx.fg.addCallbackPass<PassData>(
            PASS_NAME,
            [cameraBlock,
             hzbTexture,
             instanceBuffer,
             meshTableBuffer,
             meshletBuffer,
             modelBuffer,
             visibleMeshlets,
             visibleCount](FrameGraph::Builder& builder, PassData& pd) {
                PASS_SETUP_ZONE;

                pd.camera = builder.read(cameraBlock,
                                         framegraph::BindingInfo {
                                             .location      = {.set = 0, .binding = 0},
                                             .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                         });

                pd.hzb = hzbTexture;
                if (pd.hzb)
                {
                    pd.hzb = builder.read(pd.hzb,
                                          framegraph::TextureRead {
                                              .binding =
                                                  {
                                                      .location      = {.set = 0, .binding = 28},
                                                      .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                                  },
                                              .type        = framegraph::TextureRead::Type::eSampledImage,
                                              .imageAspect = rhi::ImageAspect::eColor,
                                          });
                }

                if (instanceBuffer)
                {
                    pd.instances = builder.read(instanceBuffer,
                                                framegraph::BindingInfo {
                                                    .location      = {.set = 0, .binding = 2},
                                                    .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                                });
                }

                if (meshTableBuffer)
                {
                    pd.meshTable = builder.read(meshTableBuffer,
                                                framegraph::BindingInfo {
                                                    .location      = {.set = 0, .binding = 3},
                                                    .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                                });
                }

                if (meshletBuffer)
                {
                    pd.meshlets = builder.read(meshletBuffer,
                                               framegraph::BindingInfo {
                                                   .location      = {.set = 0, .binding = 4},
                                                   .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                               });
                }

                if (modelBuffer)
                {
                    pd.models = builder.read(modelBuffer,
                                             framegraph::BindingInfo {
                                                 .location      = {.set = 0, .binding = 5},
                                                 .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                             });
                }

                if (visibleMeshlets)
                {
                    pd.visibleMeshlets = builder.write(visibleMeshlets,
                                                       framegraph::BindingInfo {
                                                           .location      = {.set = 0, .binding = 6},
                                                           .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                                       });
                }

                if (visibleCount)
                {
                    pd.visibleCount = builder.read(visibleCount,
                                                   framegraph::BindingInfo {
                                                       .location      = {.set = 0, .binding = 7},
                                                       .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                                   });
                }
            },
            [this](const PassData& pd, FrameGraphPassResources& resources, void* ctxPtr) {
                auto& rc = *static_cast<FrameGraphExecContext*>(ctxPtr);

                setRenderDevice(rc.rd);
                if (!rc.ext.builtinShaderLib)
                    return;
                setShaderLib(*rc.ext.builtinShaderLib);

                RHI_GPU_ZONE(rc.cb, PASS_NAME);

                auto* gpuSceneView = rc.view().gpuSceneView;
                auto* gpuSceneDb   = rc.view().gpuSceneDatabase;
                if (!gpuSceneView || !gpuSceneDb || gpuSceneView->maxVisibleMeshlets == 0u)
                {
                    rc.clear();
                    return;
                }

                const bool hasRequiredFgResources = pd.camera && pd.hzb && pd.instances && pd.meshTable &&
                                                    pd.meshlets && pd.models && pd.visibleMeshlets && pd.visibleCount;

                if (!hasRequiredFgResources)
                {
                    rc.clear();
                    return;
                }

                auto variantHash =
                    computeHighendVariantHash("meshlet_hiz_cull.comp", vshadersystem::ShaderStage::eComp, {});
                const auto* pipeline = getPipeline(variantHash);
                if (!pipeline)
                    return;

                HiZCullPushConstants pc {};
                pc.drawCount = gpuSceneView->maxVisibleMeshlets;
                if (pd.hzb)
                {
                    if (auto* hzbTexture = resources.get<framegraph::FrameGraphTexture>(pd.hzb).texture; hzbTexture)
                        pc.hzbMipCount = hzbTexture->getNumMipLevels();
                }
                if (pc.hzbMipCount == 0u)
                {
                    rc.clear();
                    return;
                }
            // MoltenVK currently hits a GPU page fault in the HiZ sampling path.
            // Keep the pass active but disable occlusion on Apple until the path is fully validated.
            // TODO(vk-moltenvk-hiz): re-enable HiZ on Apple after queue-safe sampling validation and capture repro is
            // fixed.
#if defined(__APPLE__)
                pc.enableHiZ = 0u;
#else
                pc.enableHiZ = 1u;
#endif
                if (gpuSceneDb)
                {
                    pc.instanceCount  = static_cast<uint32_t>(gpuSceneDb->instances.size());
                    pc.transformCount = static_cast<uint32_t>(gpuSceneDb->transforms.size());
                    if (gpuSceneDb->resources)
                        pc.meshletCount = static_cast<uint32_t>(gpuSceneDb->resources->meshlets.cpuMeshlets.size());
                }

                rc.cb.bindPipeline(*pipeline);
                rc.bindDescriptorSets(*pipeline);
                rc.cb.pushConstants(rhi::ShaderStages::eCompute, 0, &pc);
                rc.cb.dispatch({(pc.drawCount + 63u) / 64u, 1u, 1u});
                rc.clear();
            });
    }

    rhi::ComputePipeline MeshletHiZCullPass::createPipeline(uint64_t variantHash) const
    {
        auto shader = loadHighendShaderVariant(variantHash, vshadersystem::ShaderStage::eComp);
        if (!shader)
        {
            VULTRA_CORE_ERROR("[MeshletHiZCullPass] Failed to load compute shader variant");
            return {};
        }
        return getRenderDevice().createComputePipelineBuiltin(shader->spirv);
    }
} // namespace vultra
