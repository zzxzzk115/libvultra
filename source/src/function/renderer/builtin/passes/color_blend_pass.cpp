#include "vultra/function/renderer/builtin/passes/color_blend_pass.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"
#include "vultra/function/renderer/builtin/post_process_helper.hpp"
#include "vultra/function/renderer/renderer_render_context.hpp"

#include <shader_headers/color_blend_additive.frag.spv.h>
#include <shader_headers/color_blend_normal.frag.spv.h>

#include <fg/Blackboard.hpp>
#include <fg/FrameGraph.hpp>

namespace vultra
{
    namespace gfx
    {
        constexpr auto PASS_NAME = "ColorBlendPass";

        ColorBlendPass::ColorBlendPass(rhi::RenderDevice& rd) : rhi::RenderPass<ColorBlendPass>(rd) {}

        FrameGraphResource ColorBlendPass::blend(FrameGraph&        fg,
                                                 FrameGraphResource src,
                                                 FrameGraphResource dst,
                                                 BlendType          blendType,
                                                 ColorRange         colorRange)
        {
            const auto& extent = fg.getDescriptor<framegraph::FrameGraphTexture>(src).extent;

            struct ColorBlendData
            {
                FrameGraphResource output;
            };
            const auto& pass = fg.addCallbackPass<ColorBlendData>(
                PASS_NAME,
                [src, dst, extent, colorRange](FrameGraph::Builder& builder, ColorBlendData& data) {
                    PASS_SETUP_ZONE;

                    builder.read(src,
                                 framegraph::TextureRead {
                                     .binding =
                                         {
                                             .location      = {.set = 3, .binding = 0},
                                             .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                         },
                                     .type        = framegraph::TextureRead::Type::eCombinedImageSampler,
                                     .imageAspect = rhi::ImageAspect::eColor,
                                 });
                    builder.read(dst,
                                 framegraph::TextureRead {
                                     .binding =
                                         {
                                             .location      = {.set = 3, .binding = 1},
                                             .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                         },
                                     .type        = framegraph::TextureRead::Type::eCombinedImageSampler,
                                     .imageAspect = rhi::ImageAspect::eColor,
                                 });

                    data.output = builder.create<framegraph::FrameGraphTexture>(
                        "ColorBlendOutput",
                        {
                            .extent     = extent,
                            .format     = colorRange == ColorRange::eLDR ? rhi::PixelFormat::eRGBA8_UNorm :
                                                                           rhi::PixelFormat::eRGBA16F,
                            .usageFlags = rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled,
                        });
                    data.output = builder.write(data.output,
                                                framegraph::Attachment {
                                                    .index       = 0,
                                                    .imageAspect = rhi::ImageAspect::eColor,
                                                    .clearValue  = framegraph::ClearValue::eOpaqueBlack,
                                                });
                },
                [this, blendType](const ColorBlendData&, FrameGraphPassResources&, void* ctx) {
                    auto& rc                                    = *static_cast<gfx::RendererRenderContext*>(ctx);
                    auto& [cb, framebufferInfo, sets, samplers] = rc;

                    RHI_GPU_ZONE(cb, PASS_NAME);

                    const auto* pipeline = getPipeline(rhi::getColorFormat(*framebufferInfo, 0), blendType);
                    if (pipeline)
                    {
                        cb.bindPipeline(*pipeline);
                        assert(samplers.count("point") > 0);
                        rc.overrideSampler(sets[3][0], samplers["point"]);
                        rc.overrideSampler(sets[3][1], samplers["point"]);
                        rc.bindDescriptorSets(*pipeline);
                        cb.beginRendering(*framebufferInfo).drawFullScreenTriangle();
                        rc.endRendering();
                    }
                });

            return pass.output;
        }

        rhi::GraphicsPipeline ColorBlendPass::createPipeline(const rhi::PixelFormat colorFormat,
                                                             BlendType              blendType) const
        {
            switch (blendType)
            {
                case BlendType::eNormal:
                    return createPostProcessPipelineFromSPV(
                        getRenderDevice(), colorFormat, color_blend_normal_frag_spv);
                    break;
                case BlendType::eAdditive:
                    return createPostProcessPipelineFromSPV(
                        getRenderDevice(), colorFormat, color_blend_additive_frag_spv);

                default:
                    assert(0);
                    return {};
            }
        }
    } // namespace gfx
} // namespace vultra