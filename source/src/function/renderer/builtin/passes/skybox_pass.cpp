#include "vultra/function/renderer/builtin/passes/skybox_pass.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"
#include "vultra/function/renderer/builtin/framegraph_common.hpp"
#include "vultra/function/renderer/builtin/resources/camera_data.hpp"
#include "vultra/function/renderer/builtin/resources/depth_pre_data.hpp"
#include "vultra/function/renderer/builtin/resources/gbuffer_data.hpp"
#include "vultra/function/renderer/renderer_render_context.hpp"

#include <shader_headers/skybox.frag.spv.h>
#include <shader_headers/skybox.vert.spv.h>

#include <fg/Blackboard.hpp>
#include <fg/FrameGraph.hpp>

namespace vultra
{
    namespace gfx
    {
        constexpr auto PASS_NAME = "SkyboxPass";

        SkyboxPass::SkyboxPass(rhi::RenderDevice& rd) : rhi::RenderPass<SkyboxPass>(rd) {}

        FrameGraphResource SkyboxPass::addPass(FrameGraph&           fg,
                                               FrameGraphBlackboard& blackboard,
                                               FrameGraphResource    skyboxCubeMap,
                                               FrameGraphResource    sceneColorHDR)
        {
            auto        extent  = fg.getDescriptor<framegraph::FrameGraphTexture>(sceneColorHDR).extent;
            const auto& gBuffer = blackboard.get<GBufferData>();

            FrameGraphResource depthResource;
            if (blackboard.has<DepthPreData>())
            {
                depthResource = blackboard.get<DepthPreData>().depth;
            }
            else
            {
                depthResource = gBuffer.depth;
            }

            fg.addCallbackPass(
                PASS_NAME,
                [this, &fg, &blackboard, depthResource, skyboxCubeMap, &sceneColorHDR, extent](
                    FrameGraph::Builder& builder, auto&) {
                    PASS_SETUP_ZONE;

                    read(builder, blackboard.get<CameraData>());

                    // Read depth, use for depth testing
                    builder.read(depthResource,
                                 framegraph::Attachment {
                                     .imageAspect = rhi::ImageAspect::eDepth,
                                 });

                    // Read skybox cubemap
                    builder.read(skyboxCubeMap,
                                 framegraph::TextureRead {
                                     .binding =
                                         {
                                             .location      = {.set = 3, .binding = 0},
                                             .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                         },
                                     .type        = framegraph::TextureRead::Type::eCombinedImageSampler,
                                     .imageAspect = rhi::ImageAspect::eColor,
                                 });

                    // HDR color output
                    sceneColorHDR = builder.write(sceneColorHDR,
                                                  framegraph::Attachment {
                                                      .index       = 0,
                                                      .imageAspect = rhi::ImageAspect::eColor,
                                                  });
                },
                [this](const auto&, auto&, void* ctx) {
                    auto& rc                                    = *static_cast<gfx::RendererRenderContext*>(ctx);
                    auto& [cb, framebufferInfo, sets, samplers] = rc;
                    RHI_GPU_ZONE(cb, PASS_NAME);

                    const auto* pipeline =
                        getPipeline(rhi::getDepthFormat(*framebufferInfo), rhi::getColorFormat(*framebufferInfo, 0));
                    if (pipeline)
                    {
                        cb.bindPipeline(*pipeline);
                        assert(samplers.count("bilinear") > 0);
                        rc.overrideSampler(sets[3][0], samplers["bilinear"]); // Skybox cubemap
                        rc.bindDescriptorSets(*pipeline);
                        cb.beginRendering(*framebufferInfo).drawFullScreenTriangle();
                        rc.endRendering();
                    }
                });
            return sceneColorHDR;
        }

        rhi::GraphicsPipeline SkyboxPass::createPipeline(rhi::PixelFormat depthFormat,
                                                         rhi::PixelFormat colorFormat) const
        {
            return rhi::GraphicsPipeline::Builder {}
                .setDepthFormat(depthFormat)
                .setColorFormats({colorFormat})
                .setInputAssembly({})
                .addBuiltinShader(rhi::ShaderType::eVertex, skybox_vert_spv)
                .addBuiltinShader(rhi::ShaderType::eFragment, skybox_frag_spv)
                .setDepthStencil({
                    .depthTest      = true,
                    .depthWrite     = false,
                    .depthCompareOp = rhi::CompareOp::eLessOrEqual,
                })
                .setRasterizer({.polygonMode = rhi::PolygonMode::eFill, .cullMode = rhi::CullMode::eFront})
                .setBlending(0, {.enabled = false})
                .build(getRenderDevice());
        }
    } // namespace gfx
}; // namespace vultra