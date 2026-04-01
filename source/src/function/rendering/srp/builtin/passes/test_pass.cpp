#include "vultra/function/rendering/srp/builtin/passes/test_pass.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/structs/pixel_format.hpp"
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
        auto drawBuffer            = ctx.data.get(kResKey_DrawBuffer);
        auto indirectBuffer        = ctx.data.tryGet(kResKey_IndirectBuffer);
        auto drawSetBuffer         = ctx.data.tryGet(kResKey_DrawSetBuffer);
        auto meshletsBuffer        = ctx.data.get(kResKey_MeshletsBuffer);
        auto materialTableBuffer   = ctx.data.get(kResKey_MaterialTableBuffer);
        auto materialParamsBuffer  = ctx.data.get(kResKey_MaterialParametersBuffer);
        auto meshletVertexBuffer   = ctx.data.get(kResKey_MeshletVertexBuffer);
        auto meshletTriangleBuffer = ctx.data.get(kResKey_MeshletTriangleBuffer);

        const auto resolution  = ctx.view().extent;
        const auto cameraBlock = ctx.bb.get<CameraData>().cameraBlock.fgResource;
        const auto depthPre    = ctx.data.tryGet(kResKey_DepthTexture);

        struct PassData
        {
            FrameGraphResource camera;
            FrameGraphResource buildDone;
            FrameGraphResource depth;
            FrameGraphResource color;

            FrameGraphResource drawBuffer;
            FrameGraphResource indirectBuffer;
            FrameGraphResource drawSetBuffer;

            FrameGraphResource meshletsBuffer;
            FrameGraphResource materialTableBuffer;
            FrameGraphResource materialParamsBuffer;
            FrameGraphResource meshletVertexBuffer;
            FrameGraphResource meshletTriangleBuffer;
        };

        auto data = ctx.fg.addCallbackPass<PassData>(
            PASS_NAME,
            [resolution,
             cameraBlock,
             depthPre,
             drawBuffer,
             indirectBuffer,
             drawSetBuffer,
             meshletsBuffer,
             materialTableBuffer,
             materialParamsBuffer,
             meshletVertexBuffer,
             meshletTriangleBuffer](FrameGraph::Builder& builder, PassData& data) {
                PASS_SETUP_ZONE;

                data.camera = builder.read(cameraBlock,
                                           framegraph::BindingInfo {
                                               .location      = {.set = 0, .binding = 0},
                                               .pipelineStage = framegraph::PipelineStage::eVertexShader,
                                           });

                if (drawBuffer)
                {
                    data.drawBuffer = builder.read(drawBuffer,
                                                   framegraph::BindingInfo {
                                                       .location      = {.set = 0, .binding = 1},
                                                       .pipelineStage = framegraph::PipelineStage::eVertexShader,
                                                   });
                }

                if (indirectBuffer)
                {
                    data.indirectBuffer = builder.read(indirectBuffer,
                                                       framegraph::BindingInfo {
                                                           .location      = {},
                                                           .pipelineStage = framegraph::PipelineStage::eDrawIndirect,
                                                       });
                }

                if (drawSetBuffer)
                {
                    data.drawSetBuffer = builder.read(drawSetBuffer,
                                                      framegraph::BindingInfo {
                                                          .location      = {},
                                                          .pipelineStage = framegraph::PipelineStage::eDrawIndirect,
                                                      });
                }

                if (meshletsBuffer)
                {
                    data.meshletsBuffer = builder.read(meshletsBuffer,
                                                       framegraph::BindingInfo {
                                                           .location      = {.set = 0, .binding = 4},
                                                           .pipelineStage = framegraph::PipelineStage::eVertexShader,
                                                       });
                }

                if (materialTableBuffer)
                {
                    data.materialTableBuffer =
                        builder.read(materialTableBuffer,
                                     framegraph::BindingInfo {
                                         .location      = {.set = 0, .binding = 8},
                                         .pipelineStage = framegraph::PipelineStage::eVertexShader,
                                     });
                }

                if (materialParamsBuffer)
                {
                    data.materialParamsBuffer =
                        builder.read(materialParamsBuffer,
                                     framegraph::BindingInfo {
                                         .location      = {.set = 0, .binding = 9},
                                         .pipelineStage = framegraph::PipelineStage::eVertexShader,
                                     });
                }

                if (meshletVertexBuffer)
                {
                    data.meshletVertexBuffer =
                        builder.read(meshletVertexBuffer,
                                     framegraph::BindingInfo {
                                         .location      = {.set = 0, .binding = 10},
                                         .pipelineStage = framegraph::PipelineStage::eVertexShader,
                                     });
                }

                if (meshletTriangleBuffer)
                {
                    data.meshletTriangleBuffer =
                        builder.read(meshletTriangleBuffer,
                                     framegraph::BindingInfo {
                                         .location      = {.set = 0, .binding = 11},
                                         .pipelineStage = framegraph::PipelineStage::eVertexShader,
                                     });
                }

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
                            .extent     = resolution,
                            .format     = rhi::PixelFormat::eDepth32F,
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
                const auto* gpuSceneView     = rc.view().gpuSceneView; // temporary fallback for queueStride only

                assert(rc.framebufferInfo().has_value());
                const auto* pipeline = getPipeline(rhi::getColorFormat(rc.framebufferInfo().value(), 0));
                if (!pipeline)
                    return;

                auto* indirectBuf = data.indirectBuffer ?
                                        reinterpret_cast<rhi::DrawIndirectBuffer*>(
                                            resources.get<framegraph::FrameGraphBuffer>(data.indirectBuffer).buffer) :
                                        nullptr;

                auto* drawSetBuf = data.drawSetBuffer ?
                                       resources.get<framegraph::FrameGraphBuffer>(data.drawSetBuffer).buffer :
                                       nullptr;

                const bool supportsDrawIndirectCount = HasFlagValues(
                    rc.rd.getFeatureReport().flags, vultra::rhi::RenderDeviceFeatureReportFlagBits::eDrawIndirectCount);

                if (indirectBuf)
                {
                    rhi::prepareForDrawingIndirect(rc.cb, *indirectBuf);
                }
                if (drawSetBuf && supportsDrawIndirectCount)
                {
                    rhi::prepareForDrawingIndirect(rc.cb, *drawSetBuf);
                }

                rc.cb.beginRendering(rc.framebufferInfo().value()).bindPipeline(*pipeline);

                const bool canDrawMeshlets = renderWorld && gpuSceneDatabase && data.drawBuffer &&
                                             data.meshletsBuffer && data.materialTableBuffer &&
                                             data.materialParamsBuffer && data.meshletVertexBuffer &&
                                             data.meshletTriangleBuffer && indirectBuf;

                if (canDrawMeshlets)
                {
                    constexpr uint32_t kRenderQueueOpaque      = 0u;
                    constexpr uint32_t kRenderQueueAlphaMask   = 1u;
                    constexpr uint32_t kRenderQueueTransparent = 2u;

                    rc.resourceSet[3] = {
                        {4,
                         rhi::bindings::CombinedImageSamplerArray {
                             .textures    = gpuSceneDatabase->resources->getBindlessTextureHandles(),
                             .imageAspect = rhi::ImageAspect::eColor,
                         }},
                    };

                    rc.bindDescriptorSets(*pipeline);

                    // TODO: move this metadata fully out of gpuSceneView as well.
                    const uint32_t queueStride = gpuSceneView ? gpuSceneView->maxDraws : 0u;
                    if (queueStride == 0u)
                    {
                        rc.cb.endRendering();
                        rc.clear();
                        return;
                    }

                    const bool useIndirectCount = drawSetBuf && supportsDrawIndirectCount;

                    const auto drawQueueWindow = [&](uint32_t queueId) {
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
