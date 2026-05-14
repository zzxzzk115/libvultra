#include "vultra/function/rendering/srp/builtin/passes/general_gaussian_splat_render_pass.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/structs/pixel_format.hpp"
#include "vultra/function/framegraph/framegraph_buffer.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"
#include "vultra/function/rendering/srp/builtin/resource_keys.hpp"

#include <fg/FrameGraph.hpp>

#include <algorithm>
#include <cmath>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

namespace vultra
{
    namespace
    {
        constexpr auto PASS_NAME = "GeneralGaussianSplatRenderPass";

        struct GeneralGaussianSplatRenderPushConstants
        {
            glm::vec4 foveatedGazeAndRings {0.5f, 0.5f, 5.0f, 15.0f};
            glm::vec4 foveatedParams {1.0f, 1.0f, 2.0f, 0.0f};
            glm::vec4 targetSize {1.0f, 1.0f, 0.0f, 0.0f};
        };

        [[nodiscard]] glm::vec2 gaussianSplatFoveatedTanHalfFov(const RenderView& view)
        {
            if (!view.camera)
                return glm::vec2 {1.0f};

            const glm::mat4& projection = view.camera->projection;
            return glm::vec2 {1.0f / std::max(std::abs(projection[0][0]), 1e-5f),
                              1.0f / std::max(std::abs(projection[1][1]), 1e-5f)};
        }

        [[nodiscard]] GeneralGaussianSplatRenderPushConstants makeRenderPushConstants(
            const RenderView&                       view,
            const resource::GpuSceneView&           gpuSceneView,
            const rhi::Extent2D                     targetSize,
            const GeneralGaussianSplatFoveatedLayer layer)
        {
            const glm::vec2 tanHalfFov = gaussianSplatFoveatedTanHalfFov(view);

            GeneralGaussianSplatRenderPushConstants pc {};
            pc.foveatedGazeAndRings =
                glm::vec4 {gpuSceneView.generalGaussianSplatFoveatedGaze.x,
                           gpuSceneView.generalGaussianSplatFoveatedGaze.y,
                           gpuSceneView.generalGaussianSplatFoveatedRingDegrees.x,
                           gpuSceneView.generalGaussianSplatFoveatedRingDegrees.y};
            pc.foveatedParams = glm::vec4 {
                tanHalfFov.x,
                tanHalfFov.y,
                std::max(gpuSceneView.generalGaussianSplatFoveatedTransitionDegrees, 0.0f),
                static_cast<float>(static_cast<uint32_t>(layer)),
            };
            pc.targetSize = glm::vec4 {
                static_cast<float>(std::max(targetSize.width, 1u)),
                static_cast<float>(std::max(targetSize.height, 1u)),
                0.0f,
                0.0f,
            };
            return pc;
        }
    }

    GeneralGaussianSplatRenderPass::GeneralGaussianSplatRenderPass() { setShaderProfile(rhi::ShaderProfile::eGeneral); }

    FrameGraphResource GeneralGaussianSplatRenderPass::addPass(FrameGraphBuildContext& ctx)
    {
        return addFoveatedLayerPass(ctx, GeneralGaussianSplatFoveatedLayer::eDisabled, ctx.view().extent);
    }

    FrameGraphResource GeneralGaussianSplatRenderPass::addFoveatedLayerPass(
        FrameGraphBuildContext&                ctx,
        const GeneralGaussianSplatFoveatedLayer layer,
        const rhi::Extent2D                    resolution)
    {
        auto* gpuSceneView = ctx.view().gpuSceneView;
        if (!gpuSceneView || !gpuSceneView->hasGeneralGaussianSplats())
            return {};

        const auto layerIndex = static_cast<uint32_t>(layer);
        const bool useFoveatedLayerResources = layerIndex >= 1u && layerIndex <= 3u &&
                                               gpuSceneView->generalGaussianSplatFoveatedLayeredCompositeEnabled;
        auto visibleSplatBuffer = useFoveatedLayerResources ?
                                      ctx.data.tryGet(kResKey_GeneralGaussianSplatFoveatedVisibleSplatBuffers[layerIndex - 1u]) :
                                      ctx.data.tryGet(kResKey_GeneralGaussianSplatVisibleSplatBuffer);
        auto sortIndexBuffer = useFoveatedLayerResources ?
                                   ctx.data.tryGet(kResKey_GeneralGaussianSplatFoveatedSortIndexBuffers[layerIndex - 1u]) :
                                   ctx.data.tryGet(kResKey_GeneralGaussianSplatSortIndexBuffer);
        auto indirectBuffer = useFoveatedLayerResources ?
                                  ctx.data.tryGet(kResKey_GeneralGaussianSplatFoveatedIndirectBuffers[layerIndex - 1u]) :
                                  ctx.data.tryGet(kResKey_GeneralGaussianSplatIndirectBuffer);
        auto existingColor      = layer == GeneralGaussianSplatFoveatedLayer::eDisabled ?
                                      ctx.data.tryGet(kResKey_FinalCompositionSource) :
                                      FrameGraphResource {};
        if (!visibleSplatBuffer || !sortIndexBuffer || !indirectBuffer)
            return {};

        const bool useMultiview = ctx.view().enableMultiview && ctx.view().multiviewCameraCount >= 2u;
        const auto passName =
            layer == GeneralGaussianSplatFoveatedLayer::eDisabled ? PASS_NAME :
            layer == GeneralGaussianSplatFoveatedLayer::eFovea    ? "GeneralGaussianSplatFoveaLayerPass" :
            layer == GeneralGaussianSplatFoveatedLayer::eMid      ? "GeneralGaussianSplatMidLayerPass" :
                                                                    "GeneralGaussianSplatOuterLayerPass";
        const auto pushConstants = makeRenderPushConstants(ctx.view(), *gpuSceneView, resolution, layer);

        struct PassData
        {
            FrameGraphResource color;
            FrameGraphResource visibleSplatBuffer;
            FrameGraphResource sortIndexBuffer;
            FrameGraphResource indirectBuffer;
        };

        auto data = ctx.fg.addCallbackPass<PassData>(
            passName,
            [passName, resolution, useMultiview, visibleSplatBuffer, sortIndexBuffer, indirectBuffer, existingColor](
                FrameGraph::Builder& builder, PassData& data) {
                PASS_SETUP_ZONE;

                data.visibleSplatBuffer = builder.read(visibleSplatBuffer,
                                                       framegraph::BindingInfo {
                                                           .location      = {.set = 0, .binding = 15},
                                                           .pipelineStage = framegraph::PipelineStage::eVertexShader,
                                                       });
                data.sortIndexBuffer    = builder.read(sortIndexBuffer,
                                                    framegraph::BindingInfo {
                                                           .location      = {.set = 0, .binding = 17},
                                                           .pipelineStage = framegraph::PipelineStage::eVertexShader,
                                                    });
                data.indirectBuffer     = builder.read(indirectBuffer,
                                                   framegraph::BindingInfo {
                                                           .location      = {.set = 0, .binding = 20},
                                                           .pipelineStage = framegraph::PipelineStage::eDrawIndirect,
                                                   });

                if (existingColor)
                {
                    data.color = builder.write(existingColor,
                                               framegraph::Attachment {
                                                   .index       = 0,
                                                   .imageAspect = rhi::ImageAspect::eColor,
                                               });
                }
                else
                {
                    data.color = builder.create<framegraph::FrameGraphTexture>(
                        passName,
                        {
                            .extent     = resolution,
                            .format     = rhi::PixelFormat::eRGBA8_UNorm,
                            .layers     = useMultiview ? 2u : 0u, // 0 -> non-array texture
                            .usageFlags = rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled,
                        });
                    data.color = builder.write(data.color,
                                               framegraph::Attachment {
                                                   .index       = 0,
                                                   .imageAspect = rhi::ImageAspect::eColor,
                                                   .clearValue  = framegraph::ClearValue::eTransparentBlack,
                                               });
                }
            },
            [this, useMultiview, pushConstants](
                const PassData& data, FrameGraphPassResources& resources, void* ctxPtr) {
                VULTRA_SCOPED_FRAMEGRAPH_EXEC_CONTEXT(rc, ctxPtr);
                setRenderDevice(rc.rd);
                if (!rc.ext.builtinShaderLib)
                {
                    return;
                }
                setShaderLib(*rc.ext.builtinShaderLib);

                RHI_GPU_ZONE(rc.cb, PASS_NAME);

                if (!rc.framebufferInfo().has_value())
                {
                    return;
                }

                auto* indirectBuf = reinterpret_cast<rhi::DrawIndirectBuffer*>(
                    resources.get<framegraph::FrameGraphBuffer>(data.indirectBuffer).buffer);
                if (!indirectBuf)
                {
                    return;
                }

                rhi::prepareForDrawingIndirect(rc.cb, *indirectBuf);

                const auto* pipeline = getPipeline(rhi::getColorFormat(rc.framebufferInfo().value(), 0), useMultiview);
                if (!pipeline)
                {
                    return;
                }

                auto framebufferInfo = rc.framebufferInfo().value();
                if (useMultiview)
                {
                    framebufferInfo.layers   = 2u;
                    framebufferInfo.viewMask = 0x3u;
                }

                rc.cb.beginRendering(framebufferInfo).bindPipeline(*pipeline);
                rc.bindDescriptorSets(*pipeline);
                rc.cb.pushConstants(rhi::ShaderStages::eFragment, 0, &pushConstants);
                rc.cb.drawIndirect(rhi::DrawIndirectInfo {
                    .buffer       = indirectBuf,
                    .firstCommand = 0u,
                    .commandCount = 1u,
                });
                rc.cb.endRendering();
            });

        return data.color;
    }

    rhi::GraphicsPipeline GeneralGaussianSplatRenderPass::createPipeline(const rhi::PixelFormat colorFormat,
                                                                         const bool             useMultiview) const
    {
        rhi::ShaderLibraryRuntime::KeywordValues vertexKeywords {
            {"USE_MULTIVIEW", useMultiview ? 1u : 0u},
        };

        auto vertexShader = loadGeneralShader("gaussian_splat_render.vert", vshadersystem::ShaderStage::eVert, vertexKeywords);
        if (!vertexShader)
            return {};

        auto fragmentShader = loadGeneralShader("gaussian_splat_render.frag", vshadersystem::ShaderStage::eFrag, {});
        if (!fragmentShader)
            return {};

        auto builder = rhi::GraphicsPipeline::Builder {};
        builder.setViewMask(useMultiview ? 0x3u : 0u)
            .setColorFormats({colorFormat})
            .setTopology(rhi::PrimitiveTopology::eTriangleStrip)
            .setDepthStencil({
                .depthTest  = false,
                .depthWrite = false,
            })
            .setRasterizer({
                .polygonMode = rhi::PolygonMode::eFill,
                .cullMode    = rhi::CullMode::eNone,
            })
            .setBlending(0,
                         {
                             .enabled  = true,
                             .srcColor = rhi::BlendFactor::eOne,
                             .dstColor = rhi::BlendFactor::eOneMinusSrcAlpha,
                             .colorOp  = rhi::BlendOp::eAdd,
                             .srcAlpha = rhi::BlendFactor::eOne,
                             .dstAlpha = rhi::BlendFactor::eOneMinusSrcAlpha,
                             .alphaOp  = rhi::BlendOp::eAdd,
                         });

        if (getRenderDevice().getBackendApi() == rhi::RenderBackendApi::eWebGPU)
        {
            builder.addShader(rhi::ShaderType::eVertex,
                              rhi::ShaderStageInfo {
                                  .code           = vertexShader->wgsl,
                                  .entryPointName = "main",
                                  .defines        = {},
                                  .reflection     = vertexShader->reflection,
                              });
            builder.addShader(rhi::ShaderType::eFragment,
                              rhi::ShaderStageInfo {
                                  .code           = fragmentShader->wgsl,
                                  .entryPointName = "main",
                                  .defines        = {},
                                  .reflection     = fragmentShader->reflection,
                              });
        }
        else
        {
            builder.addBuiltinShader(rhi::ShaderType::eVertex, vertexShader->spirv)
                .addBuiltinShader(rhi::ShaderType::eFragment, fragmentShader->spirv);
        }

        return builder.build(getRenderDevice());
    }
} // namespace vultra
