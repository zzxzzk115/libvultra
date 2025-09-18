#include "vultra/function/renderer/builtin/passes/gamma_correction_pass.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"
#include "vultra/function/renderer/renderer_render_context.hpp"

#include <shader_headers/fullscreen_triangle.vert.spv.h>
#include <shader_headers/gamma.frag.spv.h>

#include <fg/Blackboard.hpp>
#include <fg/FrameGraph.hpp>

namespace vultra
{
    namespace gfx
    {
        constexpr auto PASS_NAME = "GammaCorrectionPass";

        GammaCorrectionPass::GammaCorrectionPass(rhi::RenderDevice& rd) : rhi::RenderPass<GammaCorrectionPass>(rd) {}

        FrameGraphResource
        GammaCorrectionPass::addPass(FrameGraph& fg, FrameGraphResource target, GammaCorrectionMode mode)
        {
            auto extent = fg.getDescriptor<framegraph::FrameGraphTexture>(target).extent;

            struct GammaCorrectionData
            {
                FrameGraphResource corrected;
            };
            const auto& data = fg.addCallbackPass<GammaCorrectionData>(
                PASS_NAME,
                [target, extent](FrameGraph::Builder& builder, GammaCorrectionData& data) {
                    PASS_SETUP_ZONE;

                    builder.read(target,
                                 framegraph::TextureRead {
                                     .binding =
                                         {
                                             .location      = {.set = 3, .binding = 0},
                                             .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                         },
                                     .type        = framegraph::TextureRead::Type::eCombinedImageSampler,
                                     .imageAspect = rhi::ImageAspect::eColor,
                                 });

                    data.corrected = builder.create<framegraph::FrameGraphTexture>(
                        "GammaCorrected",
                        {
                            .extent     = extent,
                            .format     = rhi::PixelFormat::eRGBA8_UNorm,
                            .usageFlags = rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled,
                        });
                    data.corrected = builder.write(data.corrected,
                                                   framegraph::Attachment {
                                                       .index       = 0,
                                                       .imageAspect = rhi::ImageAspect::eColor,
                                                       .clearValue  = framegraph::ClearValue::eOpaqueBlack,
                                                   });
                },
                [this, mode](const auto&, FrameGraphPassResources&, void* ctx) {
                    auto& rc                                    = *static_cast<gfx::RendererRenderContext*>(ctx);
                    auto& [cb, framebufferInfo, sets, samplers] = rc;

                    RHI_GPU_ZONE(cb, PASS_NAME);

                    struct PushConstants
                    {
                        int gammaCorrect {0};
                    } pushConstants;

                    pushConstants.gammaCorrect = (mode == GammaCorrectionMode::eGamma);

                    const auto* pipeline = getPipeline(rhi::getColorFormat(*framebufferInfo, 0));
                    if (pipeline)
                    {
                        cb.bindPipeline(*pipeline);
                        assert(samplers.count("point") > 0);
                        rc.overrideSampler(sets[3][0], samplers["point"]);
                        cb.pushConstants(rhi::ShaderStages::eFragment, 0, &pushConstants);
                        rc.bindDescriptorSets(*pipeline);
                        cb.beginRendering(*framebufferInfo).drawFullScreenTriangle();
                        rc.endRendering();
                    }
                });

            return data.corrected;
        }

        rhi::GraphicsPipeline GammaCorrectionPass::createPipeline(const rhi::PixelFormat colorFormat) const
        {
            return rhi::GraphicsPipeline::Builder {}
                .setColorFormats({colorFormat})
                .setInputAssembly({})
                .addBuiltinShader(rhi::ShaderType::eVertex, fullscreen_triangle_vert_spv)
                .addBuiltinShader(rhi::ShaderType::eFragment, gamma_frag_spv)
                .setDepthStencil({
                    .depthTest  = false,
                    .depthWrite = false,
                })
                .setRasterizer({
                    .polygonMode = rhi::PolygonMode::eFill,
                    .cullMode    = rhi::CullMode::eFront,
                })
                .setBlending(0, {.enabled = false})
                .build(getRenderDevice());
        }
    } // namespace gfx
} // namespace vultra