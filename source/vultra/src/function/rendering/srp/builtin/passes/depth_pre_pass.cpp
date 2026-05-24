#include "vultra/function/rendering/srp/builtin/passes/depth_pre_pass.hpp"

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
    DepthPrePass::DepthPrePass() { setShaderProfile(rhi::ShaderProfile::eHighend); }

    namespace
    {
        constexpr auto PASS_NAME = "DepthPrePass";
    } // namespace

    void DepthPrePass::addPass(FrameGraphBuildContext& ctx)
    {
        struct PassData
        {
            FrameGraphResource camera;
            FrameGraphResource drawBuffer;
            FrameGraphResource indirectBuffer;
            FrameGraphResource drawSetBuffer;
            FrameGraphResource depth;
        };

        const auto resolution     = ctx.view().extent;
        const auto cameraBlock    = ctx.bb.get<CameraData>().cameraBlock.fgResource;
        const auto drawBuffer     = ctx.data.tryGet(kResKey_DrawBuffer);
        const auto indirectBuffer = ctx.data.tryGet(kResKey_IndirectBuffer);
        const auto drawSetBuffer  = ctx.data.tryGet(kResKey_DrawSetBuffer);

        const auto data = ctx.fg.addCallbackPass<PassData>(
            PASS_NAME,
            [resolution, cameraBlock, drawBuffer, indirectBuffer, drawSetBuffer](FrameGraph::Builder& builder,
                                                                                 PassData&            pd) {
                PASS_SETUP_ZONE;

                pd.camera = builder.read(cameraBlock,
                                         framegraph::BindingInfo {
                                             .location      = {.set = 0, .binding = 0},
                                             .pipelineStage = framegraph::PipelineStage::eVertexShader,
                                         });

                pd.drawBuffer = drawBuffer;
                if (pd.drawBuffer)
                {
                    pd.drawBuffer = builder.read(pd.drawBuffer,
                                                 framegraph::BindingInfo {
                                                     .location      = {.set = 0, .binding = 1},
                                                     .pipelineStage = framegraph::PipelineStage::eVertexShader,
                                                 });
                }

                pd.indirectBuffer = indirectBuffer;
                if (pd.indirectBuffer)
                {
                    pd.indirectBuffer = builder.read(pd.indirectBuffer,
                                                     framegraph::BindingInfo {
                                                         .location      = {},
                                                         .pipelineStage = framegraph::PipelineStage::eDrawIndirect,
                                                     });
                }

                pd.drawSetBuffer = drawSetBuffer;
                if (pd.drawSetBuffer)
                {
                    pd.drawSetBuffer = builder.read(pd.drawSetBuffer,
                                                    framegraph::BindingInfo {
                                                        .location      = {.set = 0, .binding = 30},
                                                        .pipelineStage = framegraph::PipelineStage::eDrawIndirect,
                                                    });
                }

                pd.depth = builder.create<framegraph::FrameGraphTexture>(
                    "DepthPre",
                    {
                        .extent     = resolution,
                        .format     = rhi::PixelFormat::eDepth32F,
                        .usageFlags = rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled |
                                      rhi::ImageUsage::eTransferSrc,
                    });
                pd.depth = builder.write(pd.depth,
                                         framegraph::Attachment {
                                             .imageAspect = rhi::ImageAspect::eDepth,
                                             .clearValue  = framegraph::ClearValue::eOne,
                                         });
            },
            [this](const PassData& pd, FrameGraphPassResources& resources, void* ctxPtr) {
                VULTRA_SCOPED_FRAMEGRAPH_EXEC_CONTEXT(rc, ctxPtr);

                setRenderDevice(rc.rd);
                if (!rc.ext.builtinShaderLib)
                {
                    return;
                }
                setShaderLib(*rc.ext.builtinShaderLibForProfile(getShaderProfile()));

                RHI_GPU_ZONE(rc.cb, PASS_NAME);

                const auto* gpuSceneDatabase = rc.view().gpuSceneDatabase;
                const auto* gpuSceneView     = rc.view().gpuSceneView;
                auto*       cameraUbo        = resources.get<framegraph::FrameGraphBuffer>(pd.camera).buffer;

                if (!cameraUbo || !gpuSceneDatabase || !gpuSceneView || !gpuSceneDatabase->resources ||
                    !pd.drawBuffer || !pd.indirectBuffer || !gpuSceneDatabase->resources->materialTableBuffer ||
                    !gpuSceneDatabase->resources->materialParams.gpu ||
                    !gpuSceneDatabase->resources->meshlets.meshletsBuffer ||
                    !gpuSceneDatabase->resources->meshlets.meshletVerticesBuffer ||
                    !gpuSceneDatabase->resources->meshlets.meshletTrianglesBuffer || gpuSceneView->maxDraws == 0u)
                {
                    return;
                }

                assert(rc.framebufferInfo().has_value());
                const auto* pipeline = getPipeline();
                if (!pipeline)
                {
                    return;
                }

                rc.cb.beginRendering(rc.framebufferInfo().value()).bindPipeline(*pipeline);

                auto* drawBufferPtr     = resources.get<framegraph::FrameGraphBuffer>(pd.drawBuffer).buffer;
                auto* indirectBufferPtr = static_cast<rhi::DrawIndirectBuffer*>(
                    resources.get<framegraph::FrameGraphBuffer>(pd.indirectBuffer).buffer);
                auto* drawSetBufferPtr =
                    pd.drawSetBuffer ? resources.get<framegraph::FrameGraphBuffer>(pd.drawSetBuffer).buffer : nullptr;

                rhi::prepareForComputing(rc.cb, *drawBufferPtr);
                rhi::prepareForDrawingIndirect(rc.cb, *indirectBufferPtr);
                if (drawSetBufferPtr)
                    rhi::prepareForDrawingIndirect(rc.cb, *drawSetBufferPtr);
                rhi::prepareForComputing(rc.cb, *gpuSceneDatabase->resources->materialTableBuffer);
                rhi::prepareForComputing(rc.cb, *gpuSceneDatabase->resources->materialParams.gpu);
                rhi::prepareForComputing(rc.cb, *gpuSceneDatabase->resources->meshlets.meshletsBuffer);
                rhi::prepareForComputing(rc.cb, *gpuSceneDatabase->resources->meshlets.meshletVerticesBuffer);
                rhi::prepareForComputing(rc.cb, *gpuSceneDatabase->resources->meshlets.meshletTrianglesBuffer);

                rc.resourceSet[0] = {
                    {0, rhi::bindings::UniformBuffer {.buffer = cameraUbo}},
                    {1, rhi::bindings::StorageBuffer {.buffer = drawBufferPtr}},
                    {4,
                     rhi::bindings::StorageBuffer {.buffer =
                                                       gpuSceneDatabase->resources->meshlets.meshletsBuffer.get()}},
                    {8,
                     rhi::bindings::StorageBuffer {.buffer = gpuSceneDatabase->resources->materialTableBuffer.get()}},
                    {9, rhi::bindings::StorageBuffer {.buffer = gpuSceneDatabase->resources->materialParams.gpu.get()}},
                    {10,
                     rhi::bindings::StorageBuffer {
                         .buffer = gpuSceneDatabase->resources->meshlets.meshletVerticesBuffer.get()}},
                    {11,
                     rhi::bindings::StorageBuffer {
                         .buffer = gpuSceneDatabase->resources->meshlets.meshletTrianglesBuffer.get()}},
                };

                rc.resourceSet[3] = {
                    {4,
                     rhi::bindings::CombinedImageSamplerArray {
                         .textures    = gpuSceneDatabase->resources->getBindlessTextureHandles(),
                         .imageAspect = rhi::ImageAspect::eColor,
                     }},
                };

                rc.bindDescriptorSets(*pipeline);

                constexpr uint32_t kRenderQueueOpaque    = 0u;
                constexpr uint32_t kRenderQueueAlphaMask = 1u;
                const uint32_t     queueStride           = gpuSceneView->maxDraws;
                const bool         useIndirectCount =
                    drawSetBufferPtr &&
                    HasFlagValues(rc.rd.getFeatureReport().flags,
                                  vultra::rhi::RenderDeviceFeatureReportFlagBits::eDrawIndirectCount);

                const auto drawQueueWindow = [&](uint32_t queueId) {
                    const uint32_t firstCommand = queueId * queueStride;
                    if (useIndirectCount)
                    {
                        rc.cb.drawIndirectCount(
                            rhi::DrawIndirectInfo {
                                .buffer       = indirectBufferPtr,
                                .firstCommand = firstCommand,
                                .commandCount = queueStride,
                            },
                            *drawSetBufferPtr,
                            queueId * sizeof(uint32_t));
                        return;
                    }

                    if (HasFlagValues(rc.rd.getFeatureReport().flags,
                                      vultra::rhi::RenderDeviceFeatureReportFlagBits::eMultiDraw))
                    {
                        rc.cb.drawIndirect(rhi::DrawIndirectInfo {
                            .buffer       = indirectBufferPtr,
                            .firstCommand = firstCommand,
                            .commandCount = queueStride,
                        });
                    }
                    else
                    {
                        for (uint32_t i = 0; i < queueStride; ++i)
                        {
                            rc.cb.drawIndirect(rhi::DrawIndirectInfo {
                                .buffer       = indirectBufferPtr,
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

        ctx.data.set(kResKey_DepthTexture, data.depth);
    }

    rhi::GraphicsPipeline DepthPrePass::createPipeline() const
    {
        auto vertexShader = loadHighendShader("mesh.vert",
                                              vshadersystem::ShaderStage::eVert,
                                              {
                                                  {"VTX_HAS_NORMAL", 1},
                                                  {"VTX_HAS_COLOR", 0},
                                                  {"VTX_HAS_UV0", 1},
                                                  {"VTX_HAS_UV1", 0},
                                                  {"VTX_HAS_TANGENT", 1},
                                              });
        if (!vertexShader)
        {
            return {};
        }

        auto fragmentShader =
            loadHighendShader("depth_pre.frag", vshadersystem::ShaderStage::eFrag, {{"VTX_HAS_UV0", 1}});
        if (!fragmentShader)
        {
            return {};
        }

        return rhi::GraphicsPipeline::Builder {}
            .setDepthFormat(rhi::PixelFormat::eDepth32F)
            .addBuiltinShader(rhi::ShaderType::eVertex, *vertexShader)
            .addBuiltinShader(rhi::ShaderType::eFragment, *fragmentShader)
            .setDepthStencil({
                .depthTest      = true,
                .depthWrite     = true,
                .depthCompareOp = rhi::CompareOp::eLess,
            })
            .build(getRenderDevice());
    }
} // namespace vultra
