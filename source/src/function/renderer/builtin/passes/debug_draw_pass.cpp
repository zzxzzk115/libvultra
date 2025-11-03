#include "vultra/function/renderer/builtin/passes/debug_draw_pass.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"
#include "vultra/function/renderer/builtin/framegraph_common.hpp"
#include "vultra/function/renderer/builtin/resources/debug_draw_data.hpp"
#include "vultra/function/renderer/builtin/resources/depth_pre_data.hpp"
#include "vultra/function/renderer/builtin/resources/gbuffer_data.hpp"
#include "vultra/function/renderer/renderer_render_context.hpp"
#include "vultra/function/resource/raw_resource_loader.hpp"

#include <fg/Blackboard.hpp>
#include <fg/FrameGraph.hpp>

namespace vultra
{
    namespace gfx
    {
        constexpr auto PASS_NAME = "DebugDrawPass";

        DebugDrawPass::DebugDrawPass(rhi::RenderDevice& rd) : rhi::RenderPass<DebugDrawPass>(rd) {}

        void DebugDrawPass::addPass(FrameGraph&           fg,
                                    FrameGraphBlackboard& blackboard,
                                    const fsec            dt,
                                    const glm::mat4&      viewProjectionMatrix)
        {
            const auto gBuffer = blackboard.get<GBufferData>();

            FrameGraphResource depthResource;
            if (blackboard.has<DepthPreData>())
            {
                depthResource = blackboard.get<DepthPreData>().depth;
            }
            else
            {
                depthResource = gBuffer.depth;
            }

            const auto extent = fg.getDescriptor<framegraph::FrameGraphTexture>(depthResource).extent;

            const auto& debugDrawData = fg.addCallbackPass<DebugDrawData>(
                PASS_NAME,
                [&depthResource, extent](FrameGraph::Builder& builder, DebugDrawData& data) {
                    PASS_SETUP_ZONE;

                    depthResource = builder.write(depthResource,
                                                  framegraph::Attachment {
                                                      .imageAspect = rhi::ImageAspect::eDepth,
                                                  });

                    data.debugDraw = builder.create<framegraph::FrameGraphTexture>(
                        "DebugDraw - Color",
                        {
                            .extent     = extent,
                            .format     = rhi::PixelFormat::eRGBA8_UNorm,
                            .usageFlags = rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled,
                        });
                    data.debugDraw = builder.write(data.debugDraw,
                                                   framegraph::Attachment {
                                                       .index       = 0,
                                                       .imageAspect = rhi::ImageAspect::eColor,
                                                       .clearValue  = framegraph::ClearValue::eTransparentBlack,
                                                   });
                },
                [this, dt, &viewProjectionMatrix](const auto&, FrameGraphPassResources& resources, void* ctx) {
                    auto& rc                                    = *static_cast<gfx::RendererRenderContext*>(ctx);
                    auto& [cb, framebufferInfo, sets, samplers] = rc;

                    RHI_GPU_ZONE(cb, PASS_NAME);

                    // Early out if no debug draws pending
                    if (!dd::hasPendingDraws())
                    {
                        return;
                    }

                    auto* depthTexture = framebufferInfo->depthAttachment->target;
                    commonContext.debugDraw->updateColorFormat(
                        framebufferInfo->colorAttachments[0].target->getPixelFormat());
                    commonContext.debugDraw->bindDepthTexture(depthTexture);
                    commonContext.debugDraw->setViewProjectionMatrix(viewProjectionMatrix);
                    commonContext.debugDraw->buildPipelineIfNeeded();

                    commonContext.debugDraw->beginFrame(cb, *framebufferInfo);
                    dd::flush(dt.count());
                    commonContext.debugDraw->endFrame();
                });

            add(blackboard, debugDrawData);
        }
    } // namespace gfx
} // namespace vultra