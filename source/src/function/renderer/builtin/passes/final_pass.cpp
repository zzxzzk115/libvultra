#include "vultra/function/renderer/builtin/passes/final_pass.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"
#include "vultra/function/renderer/builtin/framegraph_common.hpp"
#include "vultra/function/renderer/builtin/resources/camera_data.hpp"
#include "vultra/function/renderer/builtin/resources/gbuffer_data.hpp"
#include "vultra/function/renderer/builtin/resources/scene_color_data.hpp"
#include "vultra/function/renderer/renderer_render_context.hpp"

#include <shader_headers/final_pass.frag.spv.h>
#include <shader_headers/fullscreen_triangle.vert.spv.h>

#include <fg/Blackboard.hpp>
#include <fg/FrameGraph.hpp>

namespace vultra
{
    namespace gfx
    {
        constexpr auto PASS_NAME = "FinalPass";

        namespace
        {
            enum class Mode
            {
                eDefault = 0,
                eLinearDepth,
                eRedChannel,
                eGreenChannel,
                eBlueChannel,
                eAlphaChannel,
                // eViewSpaceNormals,
                // eWorldSpaceNormals,
            };

            [[nodiscard]] auto pickOutput(const FrameGraphBlackboard& blackboard, const PassOutputMode outputMode)
            {
                auto mode {Mode::eDefault};

                std::optional<FrameGraphResource> input;
                switch (outputMode)
                {
                    using enum PassOutputMode;

                    case Albedo:
                        input = blackboard.get<GBufferData>().albedo;
                        break;
                    case Normal:
                        input = blackboard.get<GBufferData>().normal;
                        break;
                    case Emissive:
                        input = blackboard.get<GBufferData>().emissive;
                        break;
                    case Metallic:
                        mode  = Mode::eRedChannel;
                        input = blackboard.get<GBufferData>().metallicRoughnessAO;
                        break;
                    case Roughness:
                        mode  = Mode::eGreenChannel;
                        input = blackboard.get<GBufferData>().metallicRoughnessAO;
                        break;
                    case AmbientOcclusion:
                        mode  = Mode::eBlueChannel;
                        input = blackboard.get<GBufferData>().metallicRoughnessAO;
                        break;
                    case Depth:
                        mode  = Mode::eLinearDepth;
                        input = blackboard.get<GBufferData>().depth;
                        break;

                    case SceneColor_HDR:
                        input = blackboard.get<SceneColorData>().hdr;
                        break;
                    case SceneColor_LDR:
                        input = blackboard.get<SceneColorData>().ldr;
                        break;
                    case SceneColor_AntiAliased:
                        input = blackboard.get<SceneColorData>().aa;
                        break;

                    default:
                        assert(false);
                        break;
                }

                return std::tuple {input, mode};
            }

        } // namespace

        FinalPass::FinalPass(rhi::RenderDevice& rd) : rhi::RenderPass<FinalPass>(rd) {}

        FrameGraphResource FinalPass::compose(FrameGraph&           fg,
                                              FrameGraphBlackboard& blackboard,
                                              PassOutputMode        outputMode,
                                              FrameGraphResource    target)
        {
            auto [source, mode] = pickOutput(blackboard, outputMode);

            fg.addCallbackPass(
                PASS_NAME,
                [&blackboard, &target, source, mode](FrameGraph::Builder& builder, auto&) {
                    PASS_SETUP_ZONE;

                    read(builder, blackboard.get<CameraData>(), framegraph::PipelineStage::eFragmentShader);

                    if (source)
                    {
                        builder.read(*source,
                                     framegraph::TextureRead {
                                         .binding =
                                             {
                                                 .location      = {.set = 3, .binding = 0},
                                                 .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                             },
                                         .type        = framegraph::TextureRead::Type::eCombinedImageSampler,
                                         .imageAspect = mode == Mode::eLinearDepth ? rhi::ImageAspect::eDepth :
                                                                                     rhi::ImageAspect::eColor,
                                     });
                    }

                    target = builder.write(target,
                                           framegraph::Attachment {
                                               .index       = 0,
                                               .imageAspect = rhi::ImageAspect::eColor,
                                               .clearValue  = framegraph::ClearValue::eOpaqueBlack,
                                           });
                },
                [this, target, source, mode](const auto&, FrameGraphPassResources& resources, void* ctx) {
                    auto& rc                                    = *static_cast<gfx::RendererRenderContext*>(ctx);
                    auto& [cb, framebufferInfo, sets, samplers] = rc;

                    RHI_GPU_ZONE(cb, PASS_NAME);

                    const auto* pipeline = getPipeline(rhi::getColorFormat(*framebufferInfo, 0));
                    if (pipeline)
                    {
                        cb.bindPipeline(*pipeline);
                        if (source)
                        {
                            assert(samplers.count("point") > 0);
                            rc.overrideSampler(sets[3][0], samplers["point"]);
                            rc.bindDescriptorSets(*pipeline);
                            cb.pushConstants(rhi::ShaderStages::eFragment, 0, &mode);
                        }
                        cb.beginRendering(*framebufferInfo).drawFullScreenTriangle();
                        rc.endRendering();
                    }
                    auto* targetTexture = resources.get<framegraph::FrameGraphTexture>(target).texture;
                    rhi::prepareForReading(cb, *targetTexture);
                });

            return target;
        }

        rhi::GraphicsPipeline FinalPass::createPipeline(const rhi::PixelFormat colorFormat) const
        {
            return rhi::GraphicsPipeline::Builder {}
                .setColorFormats({colorFormat})
                .setInputAssembly({})
                .addBuiltinShader(rhi::ShaderType::eVertex, fullscreen_triangle_vert_spv)
                .addBuiltinShader(rhi::ShaderType::eFragment, final_pass_frag_spv)
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