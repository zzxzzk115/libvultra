#include "vultra/function/renderer/builtin/passes/fxaa_pass.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"
#include "vultra/function/renderer/builtin/post_process_helper.hpp"
#include "vultra/function/renderer/renderer_render_context.hpp"

#include <shader_headers/fxaa.frag.spv.h>

#include <fg/Blackboard.hpp>
#include <fg/FrameGraph.hpp>

#include <glm/ext/vector_float2.hpp>

namespace vultra
{
    namespace gfx
    {
        constexpr auto PASS_NAME = "FXAA Pass";

        FXAAPass::FXAAPass(rhi::RenderDevice& rd) : rhi::RenderPass<FXAAPass>(rd) {}

        FrameGraphResource FXAAPass::aa(FrameGraph& fg, FrameGraphResource src)
        {
            const auto& extent = fg.getDescriptor<framegraph::FrameGraphTexture>(src).extent;

            struct FXAAData
            {
                FrameGraphResource output;
            };
            const auto& pass = fg.addCallbackPass<FXAAData>(
                PASS_NAME,
                [src, extent](FrameGraph::Builder& builder, FXAAData& data) {
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

                    data.output = builder.create<framegraph::FrameGraphTexture>(
                        "FXAA Output",
                        {
                            .extent     = extent,
                            .format     = rhi::PixelFormat::eRGBA8_UNorm,
                            .usageFlags = rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled,
                        });
                    data.output = builder.write(data.output,
                                                framegraph::Attachment {
                                                    .index       = 0,
                                                    .imageAspect = rhi::ImageAspect::eColor,
                                                    .clearValue  = framegraph::ClearValue::eOpaqueBlack,
                                                });
                },
                [this, extent](const FXAAData&, FrameGraphPassResources&, void* ctx) {
                    auto& rc                                    = *static_cast<gfx::RendererRenderContext*>(ctx);
                    auto& [cb, framebufferInfo, sets, samplers] = rc;

                    RHI_GPU_ZONE(cb, PASS_NAME);

                    struct PushConstants
                    {
                        glm::vec2 resolution;
                    } pushConstants;

                    pushConstants.resolution = glm::vec2(extent.width, extent.height);

                    const auto* pipeline = getPipeline(rhi::getColorFormat(*framebufferInfo, 0));
                    if (pipeline)
                    {
                        cb.bindPipeline(*pipeline);
                        assert(samplers.count("bilinear") > 0);
                        rc.overrideSampler(sets[3][0], samplers["bilinear"]);
                        rc.bindDescriptorSets(*pipeline);
                        cb.pushConstants(rhi::ShaderStages::eFragment, 0, &pushConstants);
                        cb.beginRendering(*framebufferInfo).drawFullScreenTriangle();
                        rc.endRendering();
                    }
                });

            return pass.output;
        }

        rhi::GraphicsPipeline FXAAPass::createPipeline(const rhi::PixelFormat colorFormat) const
        {
            return createPostProcessPipelineFromSPV(getRenderDevice(), colorFormat, fxaa_frag_spv);
        }
    } // namespace gfx
} // namespace vultra