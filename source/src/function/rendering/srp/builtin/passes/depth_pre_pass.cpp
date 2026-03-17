#include "vultra/function/rendering/srp/builtin/passes/depth_pre_pass.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/pixel_format.hpp"
#include "vultra/function/framegraph/framegraph_buffer.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"
#include "vultra/function/rendering/srp/builtin/resource_keys.hpp"

#include <fg/FrameGraph.hpp>

namespace vultra
{
    namespace
    {
        constexpr auto PASS_NAME = "DepthPrePass";
    } // namespace

    FrameGraphResource DepthPrePass::addPass(FrameGraphBuildContext& ctx)
    {
        struct PassData
        {
            FrameGraphResource camera;
            FrameGraphResource buildDone;
            FrameGraphResource depth;
            FrameGraphResource token;
        };

        const auto resolution = ctx.view().extent;
        const auto cameraBlock = ctx.bb.get<CameraData>().cameraBlock.fgResource;
        const auto buildDone = ctx.data.tryGet(kResKey_MeshletBuildDone);

        const auto data = ctx.fg.addCallbackPass<PassData>(
            PASS_NAME,
            [resolution, cameraBlock, buildDone](FrameGraph::Builder& builder, PassData& pd) {
                PASS_SETUP_ZONE;

                pd.camera = builder.read(cameraBlock,
                                         framegraph::BindingInfo {
                                             .location      = {.set = 0, .binding = 0},
                                             .pipelineStage = framegraph::PipelineStage::eVertexShader,
                                         });

                pd.buildDone = buildDone;
                if (pd.buildDone)
                {
                    pd.buildDone = builder.read(pd.buildDone,
                                                framegraph::BindingInfo {
                                                    .location      = {.set = 0, .binding = 31},
                                                    .pipelineStage = framegraph::PipelineStage::eTransfer,
                                                });
                }

                pd.depth = builder.create<framegraph::FrameGraphTexture>(
                    "DepthPre",
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

                pd.token =
                    builder.create<framegraph::FrameGraphBuffer>("DepthPreToken",
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
            [this](const PassData& pd, FrameGraphPassResources& resources, void* ctxPtr) {
                auto& rc = *static_cast<FrameGraphExecContext*>(ctxPtr);

                setRenderDevice(rc.rd);
                if (!rc.ext.builtinShaderLib)
                    return;
                setShaderLib(*rc.ext.builtinShaderLib);

                RHI_GPU_ZONE(rc.cb, PASS_NAME);

                const auto* gpuSceneDatabase = rc.view().gpuSceneDatabase;
                const auto* gpuSceneView = rc.view().gpuSceneView;
                auto* cameraUbo = resources.get<framegraph::FrameGraphBuffer>(pd.camera).buffer;

                if (!cameraUbo || !gpuSceneDatabase || !gpuSceneView || !gpuSceneDatabase->resources ||
                    !gpuSceneView->drawBuffer || !gpuSceneView->indirectBuffer.has_value() ||
                    !gpuSceneDatabase->resources->materialTableBuffer ||
                    !gpuSceneDatabase->resources->materialParams.gpu ||
                    !gpuSceneDatabase->resources->meshlets.meshletsBuffer ||
                    !gpuSceneDatabase->resources->meshlets.meshletVerticesBuffer ||
                    !gpuSceneDatabase->resources->meshlets.meshletTrianglesBuffer ||
                    gpuSceneView->maxDraws == 0u)
                {
                    rc.clear();
                    return;
                }

                // Depth-pre uses meshlet indirect windows; skip if they are not produced yet.
                if (!pd.buildDone)
                {
                    rc.clear();
                    return;
                }

                assert(rc.framebufferInfo().has_value());
                const auto* pipeline = getPipeline();
                if (!pipeline)
                    return;

                rc.cb.beginRendering(rc.framebufferInfo().value()).bindPipeline(*pipeline);

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
                     rhi::bindings::StorageBuffer {
                         .buffer = gpuSceneDatabase->resources->meshlets.meshletsBuffer.get()}},
                    {8,
                     rhi::bindings::StorageBuffer {
                         .buffer = gpuSceneDatabase->resources->materialTableBuffer.get()}},
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

                constexpr uint32_t kRenderQueueOpaque = 0u;
                constexpr uint32_t kRenderQueueAlphaMask = 1u;
                const uint32_t queueStride = gpuSceneView->maxDraws;

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

                drawQueueWindow(kRenderQueueOpaque);
                drawQueueWindow(kRenderQueueAlphaMask);

                rc.cb.endRendering();
                rc.clear();
            });

        ctx.data.set(kResKey_DepthPreDone, data.token);
        return data.depth;
    }

    rhi::GraphicsPipeline DepthPrePass::createPipeline() const
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
        auto vertexShader = getShaderLib().load(vertexShaderVariantHash, vshadersystem::ShaderStage::eVert);
        if (!vertexShader)
        {
            VULTRA_CORE_ERROR("[DepthPrePass] Failed to load vertex shader variant");
            return {};
        }

        auto fragmentShaderVariantHash =
            getShaderLib().computeVariantHash("depth_pre.frag", vshadersystem::ShaderStage::eFrag, {{"VTX_HAS_UV0", 1}});
        auto fragmentShader = getShaderLib().load(fragmentShaderVariantHash, vshadersystem::ShaderStage::eFrag);
        if (!fragmentShader)
        {
            VULTRA_CORE_ERROR("[DepthPrePass] Failed to load fragment shader variant");
            return {};
        }

        return rhi::GraphicsPipeline::Builder {}
            .setDepthFormat(rhi::PixelFormat::eDepth32F)
            .addBuiltinShader(rhi::ShaderType::eVertex, vertexShader->spirv)
            .addBuiltinShader(rhi::ShaderType::eFragment, fragmentShader->spirv)
            .setDepthStencil({
                .depthTest      = true,
                .depthWrite     = true,
                .depthCompareOp = rhi::CompareOp::eLess,
            })
            .build(getRenderDevice());
    }
} // namespace vultra
