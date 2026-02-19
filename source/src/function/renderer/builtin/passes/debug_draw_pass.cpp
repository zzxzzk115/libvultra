#include "vultra/function/renderer/builtin/passes/debug_draw_pass.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/function/debug_draw/debug_draw_interface.hpp"
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

        DebugDrawPass::DebugDrawPass(rhi::RenderDevice& rd, DebugDrawInterface& debugDrawInterface) :
            rhi::RenderPass<DebugDrawPass>(rd), m_DebugDrawInterface(debugDrawInterface)
        {}

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
                [this, dt, &viewProjectionMatrix](const auto&, FrameGraphPassResources& /*resources*/, void* ctx) {
                    auto& rc                                    = *static_cast<gfx::RendererRenderContext*>(ctx);
                    auto& [cb, framebufferInfo, sets, samplers] = rc;

                    RHI_GPU_ZONE(cb, PASS_NAME);

                    m_DebugDrawInterface.updateColorFormat(
                        framebufferInfo->colorAttachments[0].target->getPixelFormat());
                    m_DebugDrawInterface.bindDepthTexture(framebufferInfo->depthAttachment->target);
                    m_DebugDrawInterface.setViewProjectionMatrix(viewProjectionMatrix);

                    m_DebugDrawInterface.beginFrame(cb, *framebufferInfo);
                    dd::flush();
                    m_DebugDrawInterface.endFrame();

                    // after drawing, clear the depth attachment to avoid affecting subsequent passes
                    framebufferInfo->depthAttachment = std::nullopt;
                });

            add(blackboard, debugDrawData);
        }
    } // namespace gfx
} // namespace vultra