#include "vultra/function/renderer/builtin/passes/tonemapping_pass.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"
#include "vultra/function/renderer/renderer_render_context.hpp"

#include <shader_headers/fullscreen_triangle.vert.spv.h>
#include <shader_headers/tonemapping.frag.spv.h>

#include <fg/Blackboard.hpp>
#include <fg/FrameGraph.hpp>

namespace vultra
{
    namespace gfx
    {
        constexpr auto PASS_NAME = "ToneMappingPass";

        ToneMappingPass::ToneMappingPass(rhi::RenderDevice& rd) : rhi::RenderPass<ToneMappingPass>(rd) {}

        FrameGraphResource ToneMappingPass::addPass(FrameGraph& fg, FrameGraphResource target)
        {
            auto extent = fg.getDescriptor<framegraph::FrameGraphTexture>(target).extent;

            struct ToneMappingData
            {
                FrameGraphResource tonemapped;
            };
            const auto& data = fg.addCallbackPass<ToneMappingData>(
                PASS_NAME,
                [target, extent](FrameGraph::Builder& builder, ToneMappingData& data) {
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

                    data.tonemapped = builder.create<framegraph::FrameGraphTexture>(
                        "ToneMapped",
                        {
                            .extent     = extent,
                            .format     = rhi::PixelFormat::eRGBA8_UNorm,
                            .usageFlags = rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled,
                        });
                    data.tonemapped = builder.write(data.tonemapped,
                                                    framegraph::Attachment {
                                                        .index       = 0,
                                                        .imageAspect = rhi::ImageAspect::eColor,
                                                        .clearValue  = framegraph::ClearValue::eOpaqueBlack,
                                                    });
                },
                [this](const auto&, FrameGraphPassResources&, void* ctx) {
                    auto& rc                                    = *static_cast<gfx::RendererRenderContext*>(ctx);
                    auto& [cb, framebufferInfo, sets, samplers] = rc;

                    RHI_GPU_ZONE(cb, PASS_NAME);

                    const auto* pipeline = getPipeline(rhi::getColorFormat(*framebufferInfo, 0));
                    if (pipeline)
                    {
                        cb.bindPipeline(*pipeline);
                        assert(samplers.count("point") > 0);
                        rc.overrideSampler(sets[3][0], samplers["point"]);
                        rc.bindDescriptorSets(*pipeline);
                        cb.beginRendering(*framebufferInfo).drawFullScreenTriangle();
                        rc.endRendering();
                    }
                });

            return data.tonemapped;
        }

        rhi::GraphicsPipeline ToneMappingPass::createPipeline(const rhi::PixelFormat colorFormat) const
        {
            return rhi::GraphicsPipeline::Builder {}
                .setColorFormats({colorFormat})
                .setInputAssembly({})
                .addBuiltinShader(rhi::ShaderType::eVertex, fullscreen_triangle_vert_spv)
                .addBuiltinShader(rhi::ShaderType::eFragment, tonemapping_frag_spv)
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