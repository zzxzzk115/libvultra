#include "vultra/function/rendering/srp/builtin/passes/visibility_buffer_pass.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/structs/pixel_format.hpp"
#include "vultra/function/framegraph/framegraph_buffer.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"
#include "vultra/function/rendering/srp/builtin/resource_keys.hpp"

#include <fg/FrameGraph.hpp>

namespace vultra
{
    VisibilityBufferPass::VisibilityBufferPass() { setShaderProfile(rhi::ShaderProfile::eHighend); }

    namespace
    {
        constexpr auto PASS_NAME = "VisibilityBufferPass";
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

        const auto resolution            = ctx.view().extent;
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
            [resolution,
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
                    {
                        .extent     = resolution,
                        .format     = rhi::PixelFormat::eR32UI,
                        .usageFlags = rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled,
                    });
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
                        {
                            .extent     = resolution,
                            .format     = rhi::PixelFormat::eDepth32F,
                            .usageFlags = rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled,
                        });
                    pd.depth = builder.write(pd.depth,
                                             framegraph::Attachment {
                                                 .imageAspect = rhi::ImageAspect::eDepth,
                                                 .clearValue  = framegraph::ClearValue::eOne,
                                             });
                }
            },
            [this](const PassData& pd, FrameGraphPassResources& resources, void* ctxPtr) {
                VULTRA_SCOPED_FRAMEGRAPH_EXEC_CONTEXT(rc, ctxPtr);
                setRenderDevice(rc.rd);
                if (!rc.ext.builtinShaderLib)
                    return;
                setShaderLib(*rc.ext.builtinShaderLib);

                RHI_GPU_ZONE(rc.cb, PASS_NAME);

                const auto* gpuSceneView = rc.view().gpuSceneView;
                if (!gpuSceneView || !pd.drawBuffer || !pd.indirectBuffer || !pd.meshletsBuffer ||
                    !pd.meshletVertexBuffer || !pd.meshletTriangleBuffer)
                    return;

                assert(rc.framebufferInfo().has_value());
                const auto* pipeline = getPipeline();
                if (!pipeline)
                    return;

                auto* indirectBuf = static_cast<rhi::DrawIndirectBuffer*>(
                    resources.get<framegraph::FrameGraphBuffer>(pd.indirectBuffer).buffer);
                auto* drawSetBuf =
                    pd.drawSetBuffer ? resources.get<framegraph::FrameGraphBuffer>(pd.drawSetBuffer).buffer : nullptr;

                rhi::prepareForDrawingIndirect(rc.cb, *indirectBuf);
                if (drawSetBuf)
                    rhi::prepareForDrawingIndirect(rc.cb, *drawSetBuf);

                rc.cb.beginRendering(rc.framebufferInfo().value()).bindPipeline(*pipeline);
                rc.bindDescriptorSets(*pipeline);

                constexpr uint32_t kRenderQueueOpaque    = 0u;
                constexpr uint32_t kRenderQueueAlphaMask = 1u;
                const uint32_t     queueStride           = gpuSceneView->maxDraws;
                const bool         useIndirectCount =
                    drawSetBuf &&
                    HasFlagValues(rc.rd.getFeatureReport().flags,
                                  vultra::rhi::RenderDeviceFeatureReportFlagBits::eDrawIndirectCount);

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

                drawQueueWindow(kRenderQueueOpaque);
                drawQueueWindow(kRenderQueueAlphaMask);
                rc.cb.endRendering();
            });

        ctx.data.set(kResKey_VisibilityBuffer, data.visibility);
        return data.visibility;
    }

    rhi::GraphicsPipeline VisibilityBufferPass::createPipeline() const
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
            .addBuiltinShader(rhi::ShaderType::eVertex, vertexShader->spirv)
            .addBuiltinShader(rhi::ShaderType::eFragment, fragmentShader->spirv)
            .setDepthStencil({
                .depthTest      = true,
                .depthWrite     = false,
                .depthCompareOp = rhi::CompareOp::eLessOrEqual,
            })
            .setBlending(0, {.enabled = false})
            .build(getRenderDevice());
    }
} // namespace vultra
