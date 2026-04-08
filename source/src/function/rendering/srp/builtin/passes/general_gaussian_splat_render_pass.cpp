#include "vultra/function/rendering/srp/builtin/passes/general_gaussian_splat_render_pass.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/structs/pixel_format.hpp"
#include "vultra/function/framegraph/framegraph_buffer.hpp"
#include "vultra/function/framegraph/framegraph_import.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"
#include "vultra/function/rendering/srp/builtin/resource_keys.hpp"

#include <fg/FrameGraph.hpp>

namespace vultra
{
    namespace
    {
        constexpr auto PASS_NAME = "GeneralGaussianSplatRenderPass";
    }

    GeneralGaussianSplatRenderPass::GeneralGaussianSplatRenderPass() { setShaderProfile(rhi::ShaderProfile::eGeneral); }

    FrameGraphResource GeneralGaussianSplatRenderPass::addPass(FrameGraphBuildContext& ctx)
    {
        auto* gpuSceneView = ctx.view().gpuSceneView;
        if (!gpuSceneView || !gpuSceneView->hasGeneralGaussianSplats())
            return {};

        auto visibleSplatBuffer = ctx.data.tryGet(kResKey_GeneralGaussianSplatVisibleSplatBuffer);
        auto sortIndexBuffer    = ctx.data.tryGet(kResKey_GeneralGaussianSplatSortIndexBuffer);
        auto indirectBuffer     = ctx.data.tryGet(kResKey_GeneralGaussianSplatIndirectBuffer);
        auto existingColor      = ctx.data.tryGet(kResKey_FinalCompositionSource);

        FrameGraphResource importedTarget;
        const bool         writesDirectToTarget = !existingColor && ctx.view().target;
        if (writesDirectToTarget)
            importedTarget = framegraph::importTexture(ctx.fg, "GeneralGaussianSplatTarget", ctx.view().target);

        const auto resolution   = ctx.view().extent;
        const bool useMultiview = ctx.view().enableMultiview && ctx.view().multiviewCameraCount >= 2u;

        struct PassData
        {
            FrameGraphResource color;
            FrameGraphResource visibleSplatBuffer;
            FrameGraphResource sortIndexBuffer;
            FrameGraphResource indirectBuffer;
        };

        auto data = ctx.fg.addCallbackPass<PassData>(
            PASS_NAME,
            [resolution,
             useMultiview,
             visibleSplatBuffer,
             sortIndexBuffer,
             indirectBuffer,
             existingColor,
             importedTarget](FrameGraph::Builder& builder, PassData& data) {
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
                else if (importedTarget)
                {
                    data.color = builder.write(importedTarget,
                                               framegraph::Attachment {
                                                   .index       = 0,
                                                   .imageAspect = rhi::ImageAspect::eColor,
                                               });
                }
                else
                {
                    data.color = builder.create<framegraph::FrameGraphTexture>(
                        "General Gaussian Splat Color",
                        {
                            .extent     = resolution,
                            .format     = rhi::PixelFormat::eRGBA8_UNorm,
                            .layers     = useMultiview ? 2u : 1u,
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
            [this](const PassData& data, FrameGraphPassResources& resources, void* ctxPtr) {
                auto& rc = *static_cast<FrameGraphExecContext*>(ctxPtr);
                setRenderDevice(rc.rd);
                if (!rc.ext.builtinShaderLib)
                    return;
                setShaderLib(*rc.ext.builtinShaderLib);

                RHI_GPU_ZONE(rc.cb, PASS_NAME);

                if (!rc.framebufferInfo().has_value())
                    return;

                auto* indirectBuf = reinterpret_cast<rhi::DrawIndirectBuffer*>(
                    resources.get<framegraph::FrameGraphBuffer>(data.indirectBuffer).buffer);
                if (!indirectBuf)
                    return;

                rhi::prepareForDrawingIndirect(rc.cb, *indirectBuf);

                const auto* pipeline = getPipeline(rhi::getColorFormat(rc.framebufferInfo().value(), 0));
                if (!pipeline)
                    return;

                rc.cb.beginRendering(rc.framebufferInfo().value()).bindPipeline(*pipeline);
                rc.bindDescriptorSets(*pipeline);
                rc.cb.drawIndirect(rhi::DrawIndirectInfo {
                    .buffer       = indirectBuf,
                    .firstCommand = 0u,
                    .commandCount = 1u,
                });
                rc.cb.endRendering();
                rc.clear();
            });

        return writesDirectToTarget ? FrameGraphResource {} : data.color;
    }

    rhi::GraphicsPipeline GeneralGaussianSplatRenderPass::createPipeline(const rhi::PixelFormat colorFormat) const
    {
        auto vertexShader = loadGeneralShader("gaussian_splat_render.vert", vshadersystem::ShaderStage::eVert, {});
        if (!vertexShader)
            return {};

        auto fragmentShader = loadGeneralShader("gaussian_splat_render.frag", vshadersystem::ShaderStage::eFrag, {});
        if (!fragmentShader)
            return {};

        auto builder = rhi::GraphicsPipeline::Builder {};
        builder.setColorFormats({colorFormat})
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
