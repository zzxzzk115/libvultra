#include "vultra/function/rendering/srp/builtin/passes/visibility_buffer_pass.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/structs/pixel_format.hpp"
#include "vultra/function/framegraph/framegraph_buffer.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"
#include "vultra/function/rendering/srp/builtin/resource_keys.hpp"
#include "vultra/function/rendering/srp/render_target_desc.hpp"

#include <fg/FrameGraph.hpp>

namespace vultra
{
    VisibilityBufferPass::VisibilityBufferPass() { setShaderProfile(rhi::ShaderProfile::eHighend); }

    namespace
    {
        constexpr auto PASS_NAME = "VisibilityBufferPass";

        struct VisibilityPushConstants
        {
            uint32_t maxDraws {0};
            uint32_t maxMeshlets {0};
            uint32_t maxMeshletVertices {0};
            uint32_t maxMeshletTriangles {0};
        };
    }

    FrameGraphResource VisibilityBufferPass::addPass(FrameGraphBuildContext& ctx)
    {
        struct PassData
        {
            FrameGraphResource camera;
            FrameGraphResource drawBuffer;
            FrameGraphResource indirectBuffer;
            FrameGraphResource drawSetBuffer;
            FrameGraphResource meshletsBuffer;
            FrameGraphResource meshletVertexBuffer;
            FrameGraphResource meshletTriangleBuffer;
            FrameGraphResource visibility;
            FrameGraphResource depth;
        };

        const auto visibilityDesc =
            makeRenderViewTextureDesc(ctx.view(), rhi::PixelFormat::eR32UI, rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled);
        const auto depthDesc =
            makeRenderViewTextureDesc(ctx.view(), rhi::PixelFormat::eDepth32F, rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled);
        const auto cameraBlock           = ctx.bb.get<CameraData>().cameraBlock.fgResource;
        const auto drawBuffer            = ctx.data.tryGet(kResKey_DrawBuffer);
        const auto indirectBuffer        = ctx.data.tryGet(kResKey_IndirectBuffer);
        const auto drawSetBuffer         = ctx.data.tryGet(kResKey_DrawSetBuffer);
        const auto meshletsBuffer        = ctx.data.tryGet(kResKey_MeshletsBuffer);
        const auto meshletVertexBuffer   = ctx.data.tryGet(kResKey_MeshletVertexBuffer);
        const auto meshletTriangleBuffer = ctx.data.tryGet(kResKey_MeshletTriangleBuffer);
        const auto depthPre              = ctx.data.tryGet(kResKey_DepthTexture);

        auto data = ctx.fg.addCallbackPass<PassData>(
            PASS_NAME,
            [visibilityDesc,
             depthDesc,
             cameraBlock,
             drawBuffer,
             indirectBuffer,
             drawSetBuffer,
             meshletsBuffer,
             meshletVertexBuffer,
             meshletTriangleBuffer,
             depthPre](FrameGraph::Builder& builder, PassData& pd) {
                PASS_SETUP_ZONE;

                pd.camera = builder.read(cameraBlock,
                                         framegraph::BindingInfo {
                                             .location      = {.set = 0, .binding = 0},
                                             .pipelineStage = framegraph::PipelineStage::eVertexShader,
                                         });

                if (drawBuffer)
                    pd.drawBuffer = builder.read(drawBuffer,
                                                 framegraph::BindingInfo {
                                                     .location      = {.set = 0, .binding = 1},
                                                     .pipelineStage = framegraph::PipelineStage::eVertexShader,
                                                 });
                if (indirectBuffer)
                    pd.indirectBuffer = builder.read(indirectBuffer,
                                                     framegraph::BindingInfo {
                                                         .location      = {},
                                                         .pipelineStage = framegraph::PipelineStage::eDrawIndirect,
                                                     });
                if (drawSetBuffer)
                    pd.drawSetBuffer = builder.read(drawSetBuffer,
                                                    framegraph::BindingInfo {
                                                        .location      = {},
                                                        .pipelineStage = framegraph::PipelineStage::eDrawIndirect,
                                                    });
                if (meshletsBuffer)
                    pd.meshletsBuffer = builder.read(meshletsBuffer,
                                                     framegraph::BindingInfo {
                                                         .location      = {.set = 0, .binding = 4},
                                                         .pipelineStage = framegraph::PipelineStage::eVertexShader,
                                                     });
                if (meshletVertexBuffer)
                    pd.meshletVertexBuffer =
                        builder.read(meshletVertexBuffer,
                                     framegraph::BindingInfo {
                                         .location      = {.set = 0, .binding = 10},
                                         .pipelineStage = framegraph::PipelineStage::eVertexShader,
                                     });
                if (meshletTriangleBuffer)
                    pd.meshletTriangleBuffer =
                        builder.read(meshletTriangleBuffer,
                                     framegraph::BindingInfo {
                                         .location      = {.set = 0, .binding = 11},
                                         .pipelineStage = framegraph::PipelineStage::eVertexShader,
                                     });

                pd.visibility = builder.create<framegraph::FrameGraphTexture>(
                    "VisibilityBuffer",
                    visibilityDesc);
                pd.visibility = builder.write(pd.visibility,
                                              framegraph::Attachment {
                                                  .index       = 0,
                                                  .imageAspect = rhi::ImageAspect::eColor,
                                                  .clearValue  = framegraph::ClearValue::eUIntMax,
                                              });

                if (depthPre)
                {
                    pd.depth = builder.write(depthPre,
                                             framegraph::Attachment {
                                                 .imageAspect = rhi::ImageAspect::eDepth,
                                             });
                }
                else
                {
                    pd.depth = builder.create<framegraph::FrameGraphTexture>(
                        "VisibilityDepth",
                        depthDesc);
                    pd.depth = builder.write(pd.depth,
                                             framegraph::Attachment {
                                                 .imageAspect = rhi::ImageAspect::eDepth,
                                                 .clearValue  = framegraph::ClearValue::eOne,
                                             });
                }
            },
            [this, readOnlyDepth = static_cast<bool>(depthPre)](
                const PassData& pd, FrameGraphPassResources& resources, void* ctxPtr) {
                VULTRA_SCOPED_FRAMEGRAPH_EXEC_CONTEXT(rc, ctxPtr);
                setRenderDevice(rc.rd);
                if (!rc.ext.builtinShaderLib)
                    return;
                setShaderLib(*rc.ext.builtinShaderLib);

                RHI_GPU_ZONE(rc.cb, PASS_NAME);

                const auto* gpuSceneView = rc.view().gpuSceneView;
                const auto* gpuSceneDatabase = rc.view().gpuSceneDatabase;
                if (!gpuSceneView || !gpuSceneDatabase || !gpuSceneDatabase->resources || !pd.drawBuffer ||
                    !pd.indirectBuffer || !pd.meshletsBuffer ||
                    !pd.meshletVertexBuffer || !pd.meshletTriangleBuffer)
                    return;

                assert(rc.framebufferInfo().has_value());
                const auto framebufferInfo = rc.framebufferInfo().value();
                const auto* pipeline = getPipeline(readOnlyDepth, framebufferInfo.viewMask);
                if (!pipeline)
                    return;

                auto* indirectBuf = static_cast<rhi::DrawIndirectBuffer*>(
                    resources.get<framegraph::FrameGraphBuffer>(pd.indirectBuffer).buffer);
                auto* drawSetBuf =
                    pd.drawSetBuffer ? resources.get<framegraph::FrameGraphBuffer>(pd.drawSetBuffer).buffer : nullptr;

                rhi::prepareForDrawingIndirect(rc.cb, *indirectBuf);
                if (drawSetBuf)
                    rhi::prepareForDrawingIndirect(rc.cb, *drawSetBuf);

                rc.cb.beginRendering(framebufferInfo).bindPipeline(*pipeline);
                rc.bindDescriptorSets(*pipeline);
                const VisibilityPushConstants pc {
                    .maxDraws            = gpuSceneView->maxDraws,
                    .maxMeshlets         = static_cast<uint32_t>(gpuSceneDatabase->resources->meshlets.cpuMeshlets.size()),
                    .maxMeshletVertices  = static_cast<uint32_t>(
                         gpuSceneDatabase->resources->meshlets.cpuMeshletVertices.size()),
                    .maxMeshletTriangles = static_cast<uint32_t>(
                         gpuSceneDatabase->resources->meshlets.cpuMeshletTriangles.size()),
                };
                rc.cb.pushConstants(rhi::ShaderStages::eVertex, 0, &pc);

                constexpr uint32_t kRenderQueueOpaque    = 0u;
                constexpr uint32_t kRenderQueueAlphaMask = 1u;
                const uint32_t     queueStride           = gpuSceneView->maxDraws;
                const bool         useIndirectCount = false;

                const auto drawQueueWindow = [&](const uint32_t queueId) {
                    const uint32_t firstCommand = queueId * queueStride;
                    if (useIndirectCount)
                    {
                        rc.cb.drawIndirectCount(
                            rhi::DrawIndirectInfo {
                                .buffer       = indirectBuf,
                                .firstCommand = firstCommand,
                                .commandCount = queueStride,
                            },
                            *drawSetBuf,
                            queueId * sizeof(uint32_t));
                        return;
                    }

                    if (HasFlagValues(rc.rd.getFeatureReport().flags,
                                      vultra::rhi::RenderDeviceFeatureReportFlagBits::eMultiDraw))
                    {
                        rc.cb.drawIndirect(rhi::DrawIndirectInfo {
                            .buffer       = indirectBuf,
                            .firstCommand = firstCommand,
                            .commandCount = queueStride,
                        });
                    }
                    else
                    {
                        for (uint32_t i = 0; i < queueStride; ++i)
                        {
                            rc.cb.drawIndirect(rhi::DrawIndirectInfo {
                                .buffer       = indirectBuf,
                                .firstCommand = firstCommand + i,
                                .commandCount = 1,
                            });
                        }
                    }
                };

                if (drawSetBuf || gpuSceneView->isGpuDriven())
                {
                    drawQueueWindow(kRenderQueueOpaque);
                    drawQueueWindow(kRenderQueueAlphaMask);
                }
                else
                {
                    const uint32_t commandCount = static_cast<uint32_t>(gpuSceneView->indirectCommands.size());
                    if (commandCount > 0u)
                    {
                        if (HasFlagValues(rc.rd.getFeatureReport().flags,
                                          vultra::rhi::RenderDeviceFeatureReportFlagBits::eMultiDraw))
                        {
                            rc.cb.drawIndirect(rhi::DrawIndirectInfo {
                                .buffer       = indirectBuf,
                                .firstCommand = 0u,
                                .commandCount = commandCount,
                            });
                        }
                        else
                        {
                            for (uint32_t i = 0; i < commandCount; ++i)
                            {
                                rc.cb.drawIndirect(rhi::DrawIndirectInfo {
                                    .buffer       = indirectBuf,
                                    .firstCommand = i,
                                    .commandCount = 1u,
                                });
                            }
                        }
                    }
                }
                rc.cb.endRendering();
            });

        ctx.data.set(kResKey_VisibilityBuffer, data.visibility);
        ctx.data.set(kResKey_DepthTexture, data.depth);
        return data.visibility;
    }

    rhi::GraphicsPipeline VisibilityBufferPass::createPipeline(const bool readOnlyDepth, const uint32_t viewMask) const
    {
        auto vertexShader = loadHighendShader("visibility_buffer", vshadersystem::ShaderStage::eVert);
        if (!vertexShader)
        {
            VULTRA_CORE_ERROR("[VisibilityBufferPass] Failed to load vertex shader variant");
            return {};
        }

        auto fragmentShader = loadHighendShader("visibility_buffer", vshadersystem::ShaderStage::eFrag);
        if (!fragmentShader)
        {
            VULTRA_CORE_ERROR("[VisibilityBufferPass] Failed to load fragment shader variant");
            return {};
        }

        return rhi::GraphicsPipeline::Builder {}
            .setColorFormats({rhi::PixelFormat::eR32UI})
            .setDepthFormat(rhi::PixelFormat::eDepth32F)
            .setViewMask(viewMask)
            .addBuiltinShader(rhi::ShaderType::eVertex, *vertexShader)
            .addBuiltinShader(rhi::ShaderType::eFragment, *fragmentShader)
            .setDepthStencil({
                .depthTest      = true,
                .depthWrite     = !readOnlyDepth,
                .depthCompareOp = rhi::CompareOp::eLessOrEqual,
            })
            .setBlending(0, {.enabled = false})
            .build(getRenderDevice());
    }
} // namespace vultra
