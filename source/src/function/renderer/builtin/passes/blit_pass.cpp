#include "vultra/function/renderer/builtin/passes/blit_pass.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"
#include "vultra/function/renderer/builtin/post_process_helper.hpp"
#include "vultra/function/renderer/renderer_render_context.hpp"

#include <shader_headers/blit.frag.spv.h>

#include <fg/Blackboard.hpp>
#include <fg/FrameGraph.hpp>

namespace vultra
{
    namespace gfx
    {
        constexpr auto PASS_NAME = "BlitPass";

        BlitPass::BlitPass(rhi::RenderDevice& rd) : rhi::RenderPass<BlitPass>(rd) {}

        void BlitPass::blit(FrameGraph& fg, FrameGraphResource src, FrameGraphResource dst, bool readyForReading)
        {
            fg.addCallbackPass(
                PASS_NAME,
                [&src, &dst](FrameGraph::Builder& builder, auto&) {
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

                    dst = builder.write(dst,
                                        framegraph::Attachment {
                                            .index       = 0,
                                            .imageAspect = rhi::ImageAspect::eColor,
                                            .clearValue  = framegraph::ClearValue::eOpaqueBlack,
                                        });
                },
                [this, readyForReading, dst](const auto&, FrameGraphPassResources& resources, void* ctx) {
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

                    if (readyForReading)
                    {
                        rhi::prepareForReading(cb, *resources.get<framegraph::FrameGraphTexture>(dst).texture);
                    }
                });
        }

        rhi::GraphicsPipeline BlitPass::createPipeline(const rhi::PixelFormat colorFormat) const
        {
            return createPostProcessPipelineFromSPV(getRenderDevice(), colorFormat, blit_frag_spv);
        }
    } // namespace gfx
} // namespace vultra