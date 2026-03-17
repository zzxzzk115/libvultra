#include "vultra/function/rendering/srp/builtin/passes/test_pass.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/pixel_format.hpp"
#include "vultra/function/framegraph/framegraph_buffer.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"
#include "vultra/function/rendering/srp/builtin/resource_keys.hpp"

#include <fg/FrameGraph.hpp>

namespace vultra
{
    constexpr auto PASS_NAME = "TestPass";

    FrameGraphResource TestPass::addPass(FrameGraphBuildContext& ctx)
    {
        const auto resolution  = ctx.view().extent;
        const auto cameraBlock = ctx.bb.get<CameraData>().cameraBlock.fgResource;
        const auto depthPre = ctx.data.tryGet(kResKey_DepthTexture);

        struct PassData
        {
            FrameGraphResource camera;
            FrameGraphResource buildDone;
            FrameGraphResource depth;
            FrameGraphResource color;
        };
        auto data = ctx.fg.addCallbackPass<PassData>(
            PASS_NAME,
            [resolution,
             cameraBlock,
             depthPre,
             buildDone = ctx.data.get(kResKey_MeshletBuildDone)](FrameGraph::Builder& builder, PassData& data) {
                PASS_SETUP_ZONE;

                data.camera    = builder.read(cameraBlock,
                                           framegraph::BindingInfo {
                                                  .location      = {.set = 0, .binding = 0},
                                                  .pipelineStage = framegraph::PipelineStage::eVertexShader,
                                           });
                data.buildDone = builder.read(buildDone,
                                              framegraph::BindingInfo {
                                                  .location      = {.set = 0, .binding = 31},
                                                  .pipelineStage = framegraph::PipelineStage::eTransfer,
                                              });

                data.color = builder.create<framegraph::FrameGraphTexture>(
                    "Test Pass Color",
                    {
                        .extent     = resolution,
                        .format     = rhi::PixelFormat::eRGBA8_UNorm,
                        .usageFlags = rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled,
                    });
                data.color = builder.write(data.color,
                                           framegraph::Attachment {
                                               .index       = 0,
                                               .imageAspect = rhi::ImageAspect::eColor,
                                               .clearValue  = framegraph::ClearValue::eOpaqueBlack,
                                           });

                if (depthPre)
                {
                    // Consume depth from meshlet depth prepass; do not clear so shading pass can early-reject.
                    data.depth = builder.write(depthPre,
                                               framegraph::Attachment {
                                                   .imageAspect = rhi::ImageAspect::eDepth,
                                               });
                }
                else
                {
                    data.depth = builder.create<framegraph::FrameGraphTexture>(
                        "Test Pass Depth",
                        {
                            .extent = resolution,
                            .format = rhi::PixelFormat::eDepth32F,
                            .usageFlags = rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled |
                                          rhi::ImageUsage::eTransferSrc,
                        });
                    data.depth = builder.write(data.depth,
                                               framegraph::Attachment {
                                                   .imageAspect = rhi::ImageAspect::eDepth,
                                                   .clearValue  = framegraph::ClearValue::eOne,
                                               });
                }
            },
            [this](const PassData& data, FrameGraphPassResources& resources, void* ctx) {
                auto& rc = *static_cast<FrameGraphExecContext*>(ctx);
                setRenderDevice(rc.rd);
                if (!rc.ext.builtinShaderLib)
                    return;
                setShaderLib(*rc.ext.builtinShaderLib);

                RHI_GPU_ZONE(rc.cb, PASS_NAME);

                const auto* renderWorld      = rc.view().renderWorld;
                const auto* gpuSceneDatabase = rc.view().gpuSceneDatabase;
                const auto* gpuSceneView     = rc.view().gpuSceneView;
                auto*       cameraUbo        = resources.get<framegraph::FrameGraphBuffer>(data.camera).buffer;

                assert(rc.framebufferInfo().has_value());
                const auto* pipeline = getPipeline(rhi::getColorFormat(rc.framebufferInfo().value(), 0));
                if (!pipeline)
                    return;

                rc.cb.beginRendering(rc.framebufferInfo().value()).bindPipeline(*pipeline);

                const bool canDrawMeshlets = renderWorld && gpuSceneDatabase && gpuSceneView && cameraUbo &&
                                             gpuSceneView->drawBuffer && gpuSceneDatabase->resources &&
                                             gpuSceneDatabase->resources->materialTableBuffer &&
                                             gpuSceneDatabase->resources->materialParams.gpu &&
                                             gpuSceneDatabase->resources->meshlets.meshletsBuffer &&
                                             gpuSceneDatabase->resources->meshlets.meshletVerticesBuffer &&
                                             gpuSceneDatabase->resources->meshlets.meshletTrianglesBuffer &&
                                             gpuSceneView->indirectBuffer.has_value() &&
                                             gpuSceneView->getDispatchableDrawCount() > 0;

                if (canDrawMeshlets)
                {
                    constexpr uint32_t kRenderQueueOpaque = 0u;
                    constexpr uint32_t kRenderQueueAlphaMask = 1u;
                    constexpr uint32_t kRenderQueueTransparent = 2u;

                    rhi::prepareForComputing(rc.cb, *gpuSceneView->drawBuffer);
                    rhi::prepareForComputing(rc.cb, gpuSceneView->indirectBuffer.value());
                    rhi::prepareForComputing(rc.cb, *gpuSceneDatabase->resources->materialTableBuffer);
                    rhi::prepareForComputing(rc.cb, *gpuSceneDatabase->resources->materialParams.gpu);
                    rhi::prepareForComputing(rc.cb, *gpuSceneDatabase->resources->meshlets.meshletsBuffer);
                    rhi::prepareForComputing(rc.cb, *gpuSceneDatabase->resources->meshlets.meshletVerticesBuffer);
                    rhi::prepareForComputing(rc.cb, *gpuSceneDatabase->resources->meshlets.meshletTrianglesBuffer);

                    rc.resourceSet[0] = {
                        {0, rhi::bindings::UniformBuffer {.buffer = cameraUbo}},
                        {1, rhi::bindings::StorageBuffer {.buffer = gpuSceneView->drawBuffer.get()}},
                        {4,
                         rhi::bindings::StorageBuffer {.buffer =
                                                           gpuSceneDatabase->resources->meshlets.meshletsBuffer.get()}},
                        {8,
                         rhi::bindings::StorageBuffer {.buffer =
                                                           gpuSceneDatabase->resources->materialTableBuffer.get()}},
                        {9,
                         rhi::bindings::StorageBuffer {
                             .buffer = gpuSceneDatabase->resources->materialParams.gpu.get()}},

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

                    const uint32_t queueStride = gpuSceneView->maxDraws;
                    if (queueStride == 0u)
                    {
                        rc.cb.endRendering();
                        rc.clear();
                        return;
                    }

                    const auto drawQueueWindow = [&](uint32_t queueId) {
                        const uint32_t firstCommand = queueId * queueStride;

                        if (HasFlagValues(rc.rd.getFeatureReport().flags,
                                          vultra::rhi::RenderDeviceFeatureReportFlagBits::eMultiDraw))
                        {
                            rc.cb.drawIndirect(rhi::DrawIndirectInfo {
                                .buffer       = &gpuSceneView->indirectBuffer.value(),
                                .firstCommand = firstCommand,
                                .commandCount = queueStride,
                            });
                        }
                        else
                        {
                            for (uint32_t i = 0; i < queueStride; ++i)
                            {
                                rc.cb.drawIndirect(rhi::DrawIndirectInfo {
                                    .buffer       = &gpuSceneView->indirectBuffer.value(),
                                    .firstCommand = firstCommand + i,
                                    .commandCount = 1,
                                });
                            }
                        }
                    };

                    // Opaque queues first, transparent queue later.
                    drawQueueWindow(kRenderQueueOpaque);
                    drawQueueWindow(kRenderQueueAlphaMask);
                    drawQueueWindow(kRenderQueueTransparent);
                }

                rc.cb.endRendering();
                rc.clear();
            });

        return data.color;
    }

    rhi::GraphicsPipeline TestPass::createPipeline(const rhi::PixelFormat colorFormat) const
    {
        auto vertexShaderVariantHash = getShaderLib().computeVariantHash("mesh.vert",
                                                                         vshadersystem::ShaderStage::eVert,
                                                                         {
                                                                             {"VTX_HAS_NORMAL", 1},
                                                                             {"VTX_HAS_COLOR", 0},
                                                                             {"VTX_HAS_UV0", 1},
                                                                             {"VTX_HAS_UV1", 0},
                                                                             {"VTX_HAS_TANGENT", 1},
                                                                         });
        auto vertexShader            = getShaderLib().load(vertexShaderVariantHash, vshadersystem::ShaderStage::eVert);
        if (!vertexShader)
        {
            VULTRA_CORE_ERROR("[TestPass] Failed to load vertex shader variant");
            return {};
        }

        auto fragmentShaderVariantHash =
            getShaderLib().computeVariantHash("base.frag", vshadersystem::ShaderStage::eFrag, {{"VTX_HAS_UV0", 1}});
        auto fragmentShader = getShaderLib().load(fragmentShaderVariantHash, vshadersystem::ShaderStage::eFrag);
        if (!fragmentShader)
        {
            VULTRA_CORE_ERROR("[TestPass] Failed to load fragment shader variant");
            return {};
        }

        return rhi::GraphicsPipeline::Builder {}
            .setColorFormats({colorFormat})
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
