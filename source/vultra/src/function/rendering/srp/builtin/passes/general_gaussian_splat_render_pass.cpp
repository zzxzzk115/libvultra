#include "vultra/function/rendering/srp/builtin/passes/general_gaussian_splat_render_pass.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/structs/pixel_format.hpp"
#include "vultra/function/framegraph/framegraph_buffer.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"
#include "vultra/function/rendering/render_structs.hpp"
#include "vultra/function/rendering/srp/builtin/resource_keys.hpp"

#include <fg/FrameGraph.hpp>

#include <algorithm>
#include <cmath>
#include <vector>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

namespace vultra
{
    namespace
    {
        constexpr auto PASS_NAME = "GeneralGaussianSplatRenderPass";

        struct GeneralGaussianSplatRenderUniforms
        {
            glm::vec4 foveatedGazeAndRings {0.5f, 0.5f, 5.0f, 15.0f};
            glm::vec4 foveatedParams {1.0f, 1.0f, 2.0f, 0.0f};
            glm::vec4 targetSize {1.0f, 1.0f, 0.0f, 0.0f};
            glm::uvec4 entityInfo {0u};
        };

        [[nodiscard]] uint32_t gaussianSplatEntityPickingId(const RenderView& view)
        {
            if (!view.renderWorld)
                return 0u;
            for (const auto& splat : view.renderWorld->gaussianSplats)
            {
                const uint32_t id = makeEntityPickingId(splat.entity);
                if (id != 0u)
                    return id;
            }
            return 0u;
        }

        [[nodiscard]] glm::vec2 gaussianSplatFoveatedTanHalfFov(const RenderView& view)
        {
            if (!view.camera)
                return glm::vec2 {1.0f};

            const glm::mat4& projection = view.camera->projection;
            return glm::vec2 {1.0f / std::max(std::abs(projection[0][0]), 1e-5f),
                              1.0f / std::max(std::abs(projection[1][1]), 1e-5f)};
        }

        [[nodiscard]] GeneralGaussianSplatRenderUniforms makeRenderUniforms(
            const RenderView&                       view,
            const resource::GpuSceneView&           gpuSceneView,
            const rhi::Extent2D                     targetSize,
            const GeneralGaussianSplatFoveatedLayer layer)
        {
            const glm::vec2 tanHalfFov = gaussianSplatFoveatedTanHalfFov(view);

            GeneralGaussianSplatRenderUniforms pc {};
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
            pc.entityInfo = glm::uvec4 {gaussianSplatEntityPickingId(view), 0u, 0u, 0u};
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
        auto existingEntityId   = layer == GeneralGaussianSplatFoveatedLayer::eDisabled ?
                                      ctx.data.tryGet(kResKey_GBufferEntityId) :
                                      FrameGraphResource {};
        if (!visibleSplatBuffer || !sortIndexBuffer || !indirectBuffer)
            return {};

        const bool useMultiview = ctx.view().enableMultiview && ctx.view().multiviewCameraCount >= 2u;
        const auto viewMask     = useMultiview ? ctx.view().renderTargetViewMask() : 0u;
        const auto layerCount   = useMultiview ? ctx.view().renderTargetLayerCount() : 0u;
        const bool writeEntityId = ctx.view().camera != nullptr && ctx.view().camera->debugEntityIdOutput;
        const auto passName =
            layer == GeneralGaussianSplatFoveatedLayer::eDisabled ? PASS_NAME :
            layer == GeneralGaussianSplatFoveatedLayer::eFovea    ? "GeneralGaussianSplatFoveaLayerPass" :
            layer == GeneralGaussianSplatFoveatedLayer::eMid      ? "GeneralGaussianSplatMidLayerPass" :
                                                                    "GeneralGaussianSplatOuterLayerPass";
        const auto uniformsData = makeRenderUniforms(ctx.view(), *gpuSceneView, resolution, layer);
        auto&      uniformsBuffer = m_UniformBuffers[layerIndex];
        if (!uniformsBuffer || uniformsBuffer.getSize() < sizeof(GeneralGaussianSplatRenderUniforms))
        {
            uniformsBuffer = ctx.rd.createUniformBuffer(sizeof(GeneralGaussianSplatRenderUniforms));
        }

        struct PassData
        {
            FrameGraphResource color;
            FrameGraphResource entityId;
            FrameGraphResource visibleSplatBuffer;
            FrameGraphResource sortIndexBuffer;
            FrameGraphResource indirectBuffer;
        };

        auto data = ctx.fg.addCallbackPass<PassData>(
             passName,
             [passName,
             resolution,
             useMultiview,
             viewMask,
             layerCount,
             visibleSplatBuffer,
             sortIndexBuffer,
             indirectBuffer,
             existingColor,
             existingEntityId,
             writeEntityId](
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
                            .layers     = layerCount, // 0 -> non-array texture
                            .viewMask   = viewMask,
                            .usageFlags = rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled |
                                          rhi::ImageUsage::eTransferSrc,
                        });
                    data.color = builder.write(data.color,
                                               framegraph::Attachment {
                                                   .index       = 0,
                                                   .imageAspect = rhi::ImageAspect::eColor,
                                                   .clearValue  = framegraph::ClearValue::eTransparentBlack,
                                               });
                }

                if (writeEntityId && existingEntityId)
                {
                    data.entityId = builder.write(existingEntityId,
                                                  framegraph::Attachment {
                                                      .index       = 1,
                                                      .imageAspect = rhi::ImageAspect::eColor,
                                                  });
                }
                else if (writeEntityId)
                {
                    data.entityId = builder.create<framegraph::FrameGraphTexture>(
                        "GBufferEntityId",
                        {
                            .extent     = resolution,
                            .format     = rhi::PixelFormat::eRGBA8_UNorm,
                            .layers     = layerCount,
                            .viewMask   = viewMask,
                            .usageFlags = rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled |
                                          rhi::ImageUsage::eTransferSrc,
                        });
                    data.entityId = builder.write(data.entityId,
                                                  framegraph::Attachment {
                                                      .index       = 1,
                                                      .imageAspect = rhi::ImageAspect::eColor,
                                                      .clearValue  = framegraph::ClearValue::eTransparentBlack,
                                                  });
                }
            },
            [this, useMultiview, viewMask, layerCount, writeEntityId, uniformsData, layerIndex](
                const PassData& data, FrameGraphPassResources& resources, void* ctxPtr) {
                VULTRA_SCOPED_FRAMEGRAPH_EXEC_CONTEXT(rc, ctxPtr);
                setRenderDevice(rc.rd);
                if (!rc.ext.builtinShaderLib)
                {
                    return;
                }
                setShaderLib(*rc.ext.builtinShaderLibForProfile(getShaderProfile()));

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

                auto framebufferInfo = rc.framebufferInfo().value();
                const auto entityIdFormat = writeEntityId && framebufferInfo.colorAttachments.size() > 1 ?
                                                rhi::getColorFormat(framebufferInfo, 1) :
                                                rhi::PixelFormat::eUndefined;
                const auto* pipeline = getPipeline(rhi::getColorFormat(framebufferInfo, 0),
                                                   entityIdFormat,
                                                   viewMask,
                                                   writeEntityId);
                if (!pipeline)
                {
                    return;
                }

                if (useMultiview)
                {
                    framebufferInfo.layers   = layerCount;
                    framebufferInfo.viewMask = viewMask;
                }

                auto& uniformsBuffer = m_UniformBuffers[layerIndex];
                rc.cb.update(uniformsBuffer, 0, sizeof(GeneralGaussianSplatRenderUniforms), &uniformsData);
                rc.resourceSet[1][30] = rhi::bindings::UniformBuffer {.buffer = &uniformsBuffer};
                rc.cb.beginRendering(framebufferInfo).bindPipeline(*pipeline);
                rc.bindDescriptorSets(*pipeline);
                rc.cb.drawIndirect(rhi::DrawIndirectInfo {
                    .buffer       = indirectBuf,
                    .firstCommand = 0u,
                    .commandCount = 1u,
                });
                rc.cb.endRendering();
            });

        if (layer == GeneralGaussianSplatFoveatedLayer::eDisabled && data.entityId)
            ctx.data.set(kResKey_GBufferEntityId, data.entityId);

        return data.color;
    }

    rhi::GraphicsPipeline GeneralGaussianSplatRenderPass::createPipeline(const rhi::PixelFormat colorFormat,
                                                                         const rhi::PixelFormat entityIdFormat,
                                                                         const uint32_t         viewMask,
                                                                         const bool             writeEntityId) const
    {
        const bool useMultiview = viewMask != 0u;
        // Merged single-file shader: both stages share the base id "gaussian_splat_render". The file
        // declares both permute axes (USE_MULTIVIEW drives the vertex stage, WRITE_ENTITY_ID the
        // fragment stage); pass the full set at every load so each variant is selected explicitly.
        const rhi::ShaderLibraryRuntime::KeywordValues keywords {
            {"USE_MULTIVIEW", useMultiview ? 1u : 0u},
            {"WRITE_ENTITY_ID", writeEntityId ? 1u : 0u},
        };

        auto vertexShader = loadGeneralShader("gaussian_splat_render", vshadersystem::ShaderStage::eVert, keywords);
        if (!vertexShader)
            return {};

        auto fragmentShader =
            loadGeneralShader("gaussian_splat_render", vshadersystem::ShaderStage::eFrag, keywords);
        if (!fragmentShader)
            return {};

        auto builder = rhi::GraphicsPipeline::Builder {};
        std::vector<rhi::PixelFormat> colorFormats {colorFormat};
        if (writeEntityId)
            colorFormats.push_back(entityIdFormat);
        builder.setViewMask(viewMask)
            .setColorFormats(colorFormats)
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
                         })
            ;
        if (writeEntityId)
        {
            builder.setBlending(1,
                                {
                                    .enabled = false,
                                });
        }

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
            builder.addBuiltinShader(rhi::ShaderType::eVertex, *vertexShader)
                .addBuiltinShader(rhi::ShaderType::eFragment, *fragmentShader);
        }

        return builder.build(getRenderDevice());
    }
} // namespace vultra
