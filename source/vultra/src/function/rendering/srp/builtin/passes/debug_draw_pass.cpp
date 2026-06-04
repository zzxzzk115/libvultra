#include "vultra/function/rendering/srp/builtin/passes/debug_draw_pass.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/structs/framebuffer_info.hpp"
#include "vultra/core/rhi/structs/pixel_format.hpp"
#include "vultra/function/debug_draw/debug_draw_interface.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"

#include <fg/FrameGraph.hpp>

namespace vultra
{
    namespace
    {
        constexpr auto PASS_NAME = "DebugDrawPass";
    } // namespace

    FrameGraphResource DebugDrawPass::addPass(FrameGraphBuildContext& ctx,
                                              FrameGraphResource      source,
                                              FrameGraphResource      depth,
                                              const glm::mat4&        viewProjection)
    {
        if (!commonContext.debugDraw)
            return source;

        struct PassData
        {
            FrameGraphResource output;
        };

        const auto data = ctx.fg.addCallbackPass<PassData>(
            PASS_NAME,
            [source, depth](FrameGraph::Builder& builder, PassData& pd) {
                PASS_SETUP_ZONE;
                // Read scene depth as a (read-only) depth attachment so debug lines are occluded by geometry.
                builder.read(depth,
                             framegraph::Attachment {
                                 .imageAspect = rhi::ImageAspect::eDepth,
                             });
                pd.output = builder.write(source,
                                          framegraph::Attachment {
                                              .index       = 0,
                                              .imageAspect = rhi::ImageAspect::eColor,
                                          });
            },
            [viewProjection](const PassData&, FrameGraphPassResources&, void* ctxPtr) {
                VULTRA_SCOPED_FRAMEGRAPH_EXEC_CONTEXT(rc, ctxPtr);
                if (!rc.framebufferInfo())
                    return;

                auto framebufferInfo = rc.framebufferInfo().value();
                if (!framebufferInfo.colorAttachments.empty())
                {
                    framebufferInfo.colorAttachments[0].clearValue = std::nullopt;
                    framebufferInfo.colorAttachments[0].loadOp     = rhi::AttachmentLoadOp::eLoad;
                }

                auto& debugDraw = *commonContext.debugDraw;
                debugDraw.updateColorFormat(rhi::getColorFormat(framebufferInfo, 0));
                debugDraw.setDepthTest(rhi::getDepthFormat(framebufferInfo));
                debugDraw.setViewProjectionMatrix(viewProjection);

                RHI_GPU_ZONE(rc.cb, PASS_NAME);
                debugDraw.beginFrame(rc.cb, framebufferInfo);
                dd::flush(0); // renders queued geometry and clears one-frame (duration 0) items
                debugDraw.endFrame();
            });

        return data.output;
    }
} // namespace vultra
