#include "vultra/function/rendering/srp/builtin/passes/gaussian_splat_render_pass.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/function/framegraph/framegraph_buffer.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"
#include "vultra/function/rendering/srp/builtin/resource_keys.hpp"

#include <fg/FrameGraph.hpp>

namespace vultra
{
    namespace
    {
        constexpr auto PASS_NAME = "GaussianSplatRenderPass";

        // Use higher-precision intermediate to reduce quantization/banding before final composition.
        constexpr auto kColorFormat              = rhi::PixelFormat::eRGBA16F;
        constexpr auto kDepthAccumFormat         = rhi::PixelFormat::eRGBA16F;
        constexpr auto kDepthTransmittanceFormat = rhi::PixelFormat::eRG32F;

        struct RasterPushConstants
        {
            float frustumDilation {1.10f};
            float alphaCullThreshold {1.0f / 255.0f};
            float sizeCullingMinPixels {0.25f};
            float splatScale {1.0f};
            float maxAxisPixels {2048.0f};
            float depthIsoThreshold {0.7f};
        };
    } // namespace

    FrameGraphResource GaussianSplatRenderPass::addPass(FrameGraphBuildContext&              ctx,
                                                        FrameGraphResource                   buildToken,
                                                        const GaussianSplatRendererSettings& settings,
                                                        bool                                 needsSurfaceInfo)
    {
        struct PassData
        {
            FrameGraphResource camera;
            FrameGraphResource stereoCamera;
            FrameGraphResource color;
            FrameGraphResource depthAccum;
            FrameGraphResource depthTransmittance;
            FrameGraphResource depth;
        };

        const auto resolution                = ctx.view().extent;
        const auto cameraBlock               = ctx.bb.get<CameraData>().cameraBlock.fgResource;
        const auto stereoCameraBlock         = ctx.bb.get<CameraData>().stereoCameraBlock.fgResource;
        const auto depthPre                  = ctx.data.tryGet(kResKey_DepthTexture);
        const bool useMultiview              = ctx.view().enableMultiview && ctx.view().multiviewCameraCount == 2u;
        const bool useSceneDepth             = static_cast<bool>(depthPre);
        const bool supportsFragmentInterlock = HasFlagValues(
            ctx.rd.getFeatureReport().flags, rhi::RenderDeviceFeatureReportFlagBits::eFragmentShaderInterlock);
        const bool useDepthTransmittance =
            needsSurfaceInfo && supportsFragmentInterlock && settings.enableExactDepthTransmittance;
        const RasterPushConstants basePushConstants {
            .frustumDilation      = settings.frustumDilation,
            .alphaCullThreshold   = settings.alphaCullThreshold,
            .sizeCullingMinPixels = settings.sizeCullingMinPixels,
            .splatScale           = settings.splatScale,
            .maxAxisPixels        = settings.maxAxisPixels,
            .depthIsoThreshold    = settings.depthIsoThreshold,
        };

        auto data = ctx.fg.addCallbackPass<PassData>(
            PASS_NAME,
            [buildToken,
             cameraBlock,
             stereoCameraBlock,
             resolution,
             depthPre,
             needsSurfaceInfo,
             useMultiview,
             useSceneDepth,
             useDepthTransmittance](FrameGraph::Builder& builder, PassData& pd) {
                PASS_SETUP_ZONE;

                pd.camera = builder.read(cameraBlock,
                                         framegraph::BindingInfo {
                                             .location      = {.set = 0, .binding = 0},
                                             .pipelineStage = framegraph::PipelineStage::eVertexShader,
                                         });
                if (useMultiview)
                {
                    if (stereoCameraBlock)
                    {
                        pd.stereoCamera = builder.read(stereoCameraBlock,
                                                       framegraph::BindingInfo {
                                                           .location      = {.set = 0, .binding = 31},
                                                           .pipelineStage = framegraph::PipelineStage::eVertexShader,
                                                       });
                    }
                }

                if (buildToken)
                {
                    builder.read(buildToken,
                                 framegraph::BindingInfo {
                                     .location      = {},
                                     .pipelineStage = framegraph::PipelineStage::eTransfer,
                                 });
                }

                pd.color = builder.create<framegraph::FrameGraphTexture>(
                    "GaussianSplatColor",
                    {
                        .extent     = resolution,
                        .format     = kColorFormat,
                        .layers     = useMultiview ? 2u : 0u,
                        .usageFlags = rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled,
                    });
                pd.color = builder.write(pd.color,
                                         framegraph::Attachment {
                                             .index       = 0,
                                             .imageAspect = rhi::ImageAspect::eColor,
                                             .clearValue  = framegraph::ClearValue::eTransparentBlack,
                                         });

                if (useDepthTransmittance)
                {
                    pd.depthTransmittance = builder.create<framegraph::FrameGraphTexture>(
                        "GaussianSplatDepthTransmittance",
                        {
                            .extent = resolution,
                            .format = kDepthTransmittanceFormat,
                            .layers = useMultiview ? 2u : 0u,
                            .usageFlags =
                                rhi::ImageUsage::eStorage | rhi::ImageUsage::eSampled | rhi::ImageUsage::eTransferDst,
                        });
                    pd.depthTransmittance =
                        builder.write(pd.depthTransmittance,
                                      framegraph::ImageWrite {
                                          .binding     = {.location      = {.set = 0, .binding = 22},
                                                          .pipelineStage = framegraph::PipelineStage::eFragmentShader},
                                          .imageAspect = rhi::ImageAspect::eColor,
                                      });
                }
                else if (needsSurfaceInfo)
                {
                    pd.depthAccum = builder.create<framegraph::FrameGraphTexture>(
                        "GaussianSplatDepthAccum",
                        {
                            .extent     = resolution,
                            .format     = kDepthAccumFormat,
                            .layers     = useMultiview ? 2u : 0u,
                            .usageFlags = rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled,
                        });
                    pd.depthAccum = builder.write(pd.depthAccum,
                                                  framegraph::Attachment {
                                                      .index       = 1,
                                                      .imageAspect = rhi::ImageAspect::eColor,
                                                      .clearValue  = framegraph::ClearValue::eTransparentBlack,
                                                  });
                }

                if (useSceneDepth)
                {
                    pd.depth = builder.write(depthPre,
                                             framegraph::Attachment {
                                                 .imageAspect = rhi::ImageAspect::eDepth,
                                             });
                }
            },
            [this, needsSurfaceInfo, useDepthTransmittance, useSceneDepth, useMultiview, basePushConstants](
                const PassData& pd, FrameGraphPassResources& resources, void* ctxPtr) {
                auto& rc = *static_cast<FrameGraphExecContext*>(ctxPtr);
                setRenderDevice(rc.rd);
                RHI_GPU_ZONE(rc.cb, PASS_NAME);

                auto* gpuSceneView     = rc.view().gpuSceneView;
                auto* gpuSceneDatabase = rc.view().gpuSceneDatabase;
                auto* cameraUbo        = resources.get<framegraph::FrameGraphBuffer>(pd.camera).buffer;
                auto* stereoCameraUbo =
                    pd.stereoCamera ? resources.get<framegraph::FrameGraphBuffer>(pd.stereoCamera).buffer : nullptr;
                if (useDepthTransmittance)
                {
                    auto* depthTransmittanceTexture =
                        resources.get<framegraph::FrameGraphTexture>(pd.depthTransmittance).texture;
                    rc.cb.clear(*depthTransmittanceTexture, glm::vec4 {0.0f, 1.0f, 0.0f, 0.0f});
                }

                assert(rc.framebufferInfo().has_value());
                auto framebufferInfo = rc.framebufferInfo().value();
                if (useMultiview)
                {
                    framebufferInfo.layers   = 2u;
                    framebufferInfo.viewMask = 0x3u;
                }
                rc.cb.beginRendering(framebufferInfo);

                const bool canDraw = rc.ext.builtinShaderLib && gpuSceneView && cameraUbo && gpuSceneDatabase &&
                                     gpuSceneDatabase->resources && gpuSceneView->gaussianSplatDrawBuffer &&
                                     gpuSceneView->gaussianSplatSortValuesBuffer &&
                                     gpuSceneView->gaussianSplatPointDrawIdBuffer &&
                                     gpuSceneView->gaussianSplatIndirectBuffer.has_value() &&
                                     gpuSceneView->maxGaussianSplatSortElements > 0u;

                static bool s_LoggedGaussianSplatRender = false;
                if (!s_LoggedGaussianSplatRender)
                {
                    VULTRA_CORE_INFO("[GaussianSplat] render canDraw={} draws={} dispatchable={} hasPointDrawIds={} "
                                     "hasValues={}",
                                     canDraw,
                                     gpuSceneView ? gpuSceneView->gaussianSplatDraws.size() : 0u,
                                     gpuSceneView ? gpuSceneView->getDispatchableGaussianSplatDrawCount() : 0u,
                                     gpuSceneView && gpuSceneView->gaussianSplatPointDrawIdBuffer ? 1u : 0u,
                                     gpuSceneView && gpuSceneView->gaussianSplatSortValuesBuffer ? 1u : 0u);
                    s_LoggedGaussianSplatRender = true;
                }

                if (canDraw)
                {
                    setShaderLib(*rc.ext.builtinShaderLib);

                    auto       variantHash = getShaderLib().computeVariantHash("gaussian_splat.vert",
                                                                         vshadersystem::ShaderStage::eVert,
                                                                               {{"USE_MULTIVIEW", useMultiview ? 1u : 0u}});
                    const bool useFragmentInterlock =
                        useDepthTransmittance &&
                        HasFlagValues(rc.rd.getFeatureReport().flags,
                                      rhi::RenderDeviceFeatureReportFlagBits::eFragmentShaderInterlock);
                    const auto* pipeline = getPipeline(variantHash,
                                                       needsSurfaceInfo,
                                                       useDepthTransmittance,
                                                       useFragmentInterlock,
                                                       useSceneDepth,
                                                       useMultiview);

                    if (pipeline)
                    {
                        const auto& splatStorage = gpuSceneDatabase->resources->gaussianStorage;
                        if (!splatStorage.centersBuffer || !splatStorage.covarianceBuffer ||
                            !splatStorage.colorBuffer || !splatStorage.shBuffer ||
                            !gpuSceneDatabase->resources->gaussianSplatMetaBuffer)
                        {
                            rc.cb.endRendering();
                            rc.clear();
                            return;
                        }

                        rhi::prepareForReading(rc.cb, *gpuSceneView->gaussianSplatSortValuesBuffer);
                        rhi::prepareForReading(rc.cb, *gpuSceneView->gaussianSplatDrawBuffer);
                        rhi::prepareForReading(rc.cb, *gpuSceneView->gaussianSplatPointDrawIdBuffer);
                        rhi::prepareForReading(rc.cb, *splatStorage.centersBuffer);
                        rhi::prepareForReading(rc.cb, *splatStorage.covarianceBuffer);
                        rhi::prepareForReading(rc.cb, *splatStorage.colorBuffer);
                        rhi::prepareForReading(rc.cb, *splatStorage.shBuffer);
                        rhi::prepareForReading(rc.cb, *gpuSceneDatabase->resources->gaussianSplatMetaBuffer);

                        rc.cb.bindPipeline(*pipeline);
                        rc.resourceSet[0] = {
                            {0, rhi::bindings::UniformBuffer {.buffer = cameraUbo}},
                            {1, rhi::bindings::StorageBuffer {.buffer = gpuSceneView->gaussianSplatDrawBuffer.get()}},
                            {13, rhi::bindings::StorageBuffer {.buffer = splatStorage.centersBuffer.get()}},
                            {14, rhi::bindings::StorageBuffer {.buffer = splatStorage.covarianceBuffer.get()}},
                            {15, rhi::bindings::StorageBuffer {.buffer = splatStorage.colorBuffer.get()}},
                            {16, rhi::bindings::StorageBuffer {.buffer = splatStorage.shBuffer.get()}},
                            {19,
                             rhi::bindings::StorageBuffer {
                                 .buffer = gpuSceneDatabase->resources->gaussianSplatMetaBuffer.get()}},
                            {18,
                             rhi::bindings::StorageBuffer {.buffer =
                                                               gpuSceneView->gaussianSplatSortValuesBuffer.get()}},
                            {21,
                             rhi::bindings::StorageBuffer {.buffer =
                                                               gpuSceneView->gaussianSplatPointDrawIdBuffer.get()}},
                        };
                        if (useMultiview && stereoCameraUbo)
                            rc.resourceSet[0][31] = rhi::bindings::UniformBuffer {.buffer = stereoCameraUbo};
                        if (useDepthTransmittance)
                        {
                            rc.resourceSet[0][22] = rhi::bindings::StorageImage {
                                .texture = resources.get<framegraph::FrameGraphTexture>(pd.depthTransmittance).texture,
                                .imageAspect = rhi::ImageAspect::eColor,
                                .mipLevel    = 0,
                            };
                        }
                        rc.bindDescriptorSets(*pipeline);
                        const RasterPushConstants pc = basePushConstants;
                        rc.cb.pushConstants(rhi::ShaderStages::eVertex | rhi::ShaderStages::eFragment, 0, &pc);
                        rc.cb.drawIndirect(rhi::DrawIndirectInfo {
                            .buffer       = &gpuSceneView->gaussianSplatIndirectBuffer.value(),
                            .firstCommand = 0,
                            .commandCount = 1,
                        });
                    }
                }

                rc.cb.endRendering();
                rc.clear();
            });

        if (data.depthAccum)
            ctx.data.set(kResKey_GaussianSplatDepthAccum, data.depthAccum);
        if (data.depthTransmittance)
            ctx.data.set(kResKey_GaussianSplatDepthTransmittance, data.depthTransmittance);
        return data.color;
    }

    rhi::GraphicsPipeline GaussianSplatRenderPass::createPipeline(uint64_t variantHash,
                                                                  bool     needsSurfaceInfo,
                                                                  bool     useDepthTransmittance,
                                                                  bool     useFragmentInterlock,
                                                                  bool     useSceneDepth,
                                                                  bool     useMultiview) const
    {
        auto vertexShader = getShaderLib().load(variantHash, vshadersystem::ShaderStage::eVert);
        if (!vertexShader)
        {
            VULTRA_CORE_ERROR("[GaussianSplatRenderPass] Failed to load vertex shader variant");
            return {};
        }

        constexpr uint32_t kOutputSrgb =
            (kColorFormat == rhi::PixelFormat::eRGBA8_sRGB || kColorFormat == rhi::PixelFormat::eBGRA8_sRGB) ? 1u : 0u;
        auto fragmentShaderVariantHash =
            getShaderLib().computeVariantHash("gaussian_splat.frag",
                                              vshadersystem::ShaderStage::eFrag,
                                              {{"SPLAT_OUTPUT_SRGB", kOutputSrgb},
                                               {"NEED_SURFACE_INFO", needsSurfaceInfo ? 1u : 0u},
                                               {"USE_DEPTH_TRANSMITTANCE", useDepthTransmittance ? 1u : 0u},
                                               {"USE_FRAGMENT_INTERLOCK", useFragmentInterlock ? 1u : 0u}});
        auto fragmentShader = getShaderLib().load(fragmentShaderVariantHash, vshadersystem::ShaderStage::eFrag);
        if (!fragmentShader)
        {
            VULTRA_CORE_ERROR("[GaussianSplatRenderPass] Failed to load fragment shader variant");
            return {};
        }

        rhi::GraphicsPipeline::Builder builder {};
        builder.setViewMask(useMultiview ? 0x3u : 0u);
        if (!needsSurfaceInfo)
            builder.setColorFormats({kColorFormat});
        else if (useDepthTransmittance)
            builder.setColorFormats({kColorFormat});
        else
            builder.setColorFormats({kColorFormat, kDepthAccumFormat});
        if (useSceneDepth)
            builder.setDepthFormat(rhi::PixelFormat::eDepth32F);
        builder.setInputAssembly({})
            .setTopology(rhi::PrimitiveTopology::eTriangleStrip)
            .addBuiltinShader(rhi::ShaderType::eVertex, vertexShader->spirv)
            .addBuiltinShader(rhi::ShaderType::eFragment, fragmentShader->spirv)
            .setDepthStencil({
                .depthTest      = useSceneDepth,
                .depthWrite     = false,
                .depthCompareOp = rhi::CompareOp::eLessOrEqual,
            })
            .setRasterizer({
                .polygonMode = rhi::PolygonMode::eFill,
                .cullMode    = rhi::CullMode::eNone,
            })
            .setBlending(0,
                         {
                             .enabled  = true,
                             .srcColor = rhi::BlendFactor::eOneMinusDstAlpha,
                             .dstColor = rhi::BlendFactor::eOne,
                             .colorOp  = rhi::BlendOp::eAdd,
                             .srcAlpha = rhi::BlendFactor::eOneMinusDstAlpha,
                             .dstAlpha = rhi::BlendFactor::eOne,
                             .alphaOp  = rhi::BlendOp::eAdd,
                         });

        if (needsSurfaceInfo && !useDepthTransmittance)
        {
            builder.setBlending(1,
                                {
                                    .enabled  = true,
                                    .srcColor = rhi::BlendFactor::eOneMinusDstAlpha,
                                    .dstColor = rhi::BlendFactor::eOne,
                                    .colorOp  = rhi::BlendOp::eAdd,
                                    .srcAlpha = rhi::BlendFactor::eOneMinusDstAlpha,
                                    .dstAlpha = rhi::BlendFactor::eOne,
                                    .alphaOp  = rhi::BlendOp::eAdd,
                                });
        }

        return builder.build(getRenderDevice());
    }
} // namespace vultra
