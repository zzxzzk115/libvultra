#include "vultra/function/renderer/builtin/passes/deferred_lighting_pass.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/function/framegraph/framegraph_import.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"
#include "vultra/function/renderer/builtin/framegraph_common.hpp"
#include "vultra/function/renderer/builtin/resources/camera_data.hpp"
#include "vultra/function/renderer/builtin/resources/gbuffer_data.hpp"
#include "vultra/function/renderer/builtin/resources/light_data.hpp"
#include "vultra/function/renderer/builtin/resources/scene_color_data.hpp"
#include "vultra/function/renderer/renderer_render_context.hpp"
#include "vultra/function/resource/raw_resource_loader.hpp"

#include <shader_headers/deferred_lighting.frag.spv.h>
#include <shader_headers/fullscreen_triangle.vert.spv.h>

#include <texture_headers/ltc_1.dds.bintex.h>
#include <texture_headers/ltc_2.dds.bintex.h>

#include <fg/Blackboard.hpp>
#include <fg/FrameGraph.hpp>

namespace vultra
{
    namespace gfx
    {
        constexpr auto PASS_NAME = "DeferredLightingPass";

        DeferredLightingPass::DeferredLightingPass(rhi::RenderDevice& rd) : rhi::RenderPass<DeferredLightingPass>(rd)
        {
            // Load builtin LTC lookup textures
            auto ltc_1 = resource::loadTextureKTX_DDS_Raw(ltc_1_dds_bintex, rd);
            VULTRA_CORE_ASSERT(ltc_1, "[Builtin-DeferredLightingPass] Failed to load LTC 1 texture");
            m_LTCMat = createRef<vultra::rhi::Texture>(std::move(ltc_1.value()));

            auto ltc_2 = resource::loadTextureKTX_DDS_Raw(ltc_2_dds_bintex, rd);
            VULTRA_CORE_ASSERT(ltc_2, "[Builtin-DeferredLightingPass] Failed to load LTC 2 texture");
            m_LTCMag = createRef<vultra::rhi::Texture>(std::move(ltc_2.value()));
        }

        void DeferredLightingPass::addPass(FrameGraph&           fg,
                                           FrameGraphBlackboard& blackboard,
                                           bool                  enableAreaLight,
                                           glm::vec4             clearColor)
        {
            const auto gBuffer = blackboard.get<GBufferData>();
            const auto extent  = fg.getDescriptor<framegraph::FrameGraphTexture>(gBuffer.depth).extent;

            // Disable area lights if LUTs missing
            if (enableAreaLight && (!m_LTCMat || !m_LTCMag))
            {
                enableAreaLight = false;
            }

            const auto& sceneColorData = fg.addCallbackPass<SceneColorData>(
                PASS_NAME,
                [this, &blackboard, &fg, extent, gBuffer, enableAreaLight](FrameGraph::Builder& builder,
                                                                           SceneColorData&      data) {
                    PASS_SETUP_ZONE;

                    read(builder, blackboard.get<CameraData>());
                    read(builder, blackboard.get<LightData>());

                    // Read from G-Buffer

                    // Albedo
                    builder.read(gBuffer.albedo,
                                 framegraph::TextureRead {
                                     .binding =
                                         {
                                             .location      = {.set = 3, .binding = 0},
                                             .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                         },
                                     .type        = framegraph::TextureRead::Type::eCombinedImageSampler,
                                     .imageAspect = rhi::ImageAspect::eColor,
                                 });

                    // Normal
                    builder.read(gBuffer.normal,
                                 framegraph::TextureRead {
                                     .binding =
                                         {
                                             .location      = {.set = 3, .binding = 1},
                                             .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                         },
                                     .type        = framegraph::TextureRead::Type::eCombinedImageSampler,
                                     .imageAspect = rhi::ImageAspect::eColor,
                                 });

                    // Emissive
                    builder.read(gBuffer.emissive,
                                 framegraph::TextureRead {
                                     .binding =
                                         {
                                             .location      = {.set = 3, .binding = 2},
                                             .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                         },
                                     .type        = framegraph::TextureRead::Type::eCombinedImageSampler,
                                     .imageAspect = rhi::ImageAspect::eColor,
                                 });

                    // MetallicRoughnessAO
                    builder.read(gBuffer.metallicRoughnessAO,
                                 framegraph::TextureRead {
                                     .binding =
                                         {
                                             .location      = {.set = 3, .binding = 3},
                                             .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                         },
                                     .type        = framegraph::TextureRead::Type::eCombinedImageSampler,
                                     .imageAspect = rhi::ImageAspect::eColor,
                                 });

                    // Depth
                    builder.read(gBuffer.depth,
                                 framegraph::TextureRead {
                                     .binding =
                                         {
                                             .location      = {.set = 3, .binding = 4},
                                             .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                         },
                                     .type        = framegraph::TextureRead::Type::eCombinedImageSampler,
                                     .imageAspect = rhi::ImageAspect::eDepth,
                                 });

                    // LTC lookup textures for area lights
                    auto ltcMat = framegraph::importTexture(fg, "LTCMat", m_LTCMat.get());
                    auto ltcMag = framegraph::importTexture(fg, "LTCMag", m_LTCMag.get());
                    builder.read(ltcMat,
                                 framegraph::TextureRead {
                                     .binding =
                                         {
                                             .location      = {.set = 3, .binding = 5},
                                             .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                         },
                                     .type        = framegraph::TextureRead::Type::eCombinedImageSampler,
                                     .imageAspect = rhi::ImageAspect::eColor,
                                 });
                    builder.read(ltcMag,
                                 framegraph::TextureRead {
                                     .binding =
                                         {
                                             .location      = {.set = 3, .binding = 6},
                                             .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                         },
                                     .type        = framegraph::TextureRead::Type::eCombinedImageSampler,
                                     .imageAspect = rhi::ImageAspect::eColor,
                                 });

                    data.hdr = builder.create<framegraph::FrameGraphTexture>(
                        "SceneColor - HDR",
                        {
                            .extent     = extent,
                            .format     = rhi::PixelFormat::eRGBA16F,
                            .usageFlags = rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled,
                        });
                    data.hdr = builder.write(data.hdr,
                                             framegraph::Attachment {
                                                 .index       = 0,
                                                 .imageAspect = rhi::ImageAspect::eColor,
                                             });

                    data.bright = builder.create<framegraph::FrameGraphTexture>(
                        "SceneColor - Bright",
                        {
                            .extent     = extent,
                            .format     = rhi::PixelFormat::eRGBA16F,
                            .usageFlags = rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled,
                        });
                    data.bright = builder.write(data.bright,
                                                framegraph::Attachment {
                                                    .index       = 1,
                                                    .imageAspect = rhi::ImageAspect::eColor,
                                                    .clearValue  = framegraph::ClearValue::eTransparentBlack,
                                                });
                },
                [this, enableAreaLight, clearColor](const SceneColorData&, auto&, void* ctx) {
                    auto& rc                                    = *static_cast<gfx::RendererRenderContext*>(ctx);
                    auto& [cb, framebufferInfo, sets, samplers] = rc;
                    RHI_GPU_ZONE(cb, PASS_NAME);

                    // Post set clear color
                    assert(framebufferInfo && framebufferInfo->colorAttachments.size() > 0);
                    framebufferInfo->colorAttachments[0].clearValue = clearColor;

                    struct PushConstants
                    {
                        int enableAreaLight;
                    };

                    PushConstants pushConstants {enableAreaLight};

                    const auto* pipeline = getPipeline();
                    if (pipeline)
                    {
                        cb.bindPipeline(*pipeline);
                        assert(samplers.count("point") > 0);
                        // assert(samplers.count("shadow_map") > 0);
                        assert(samplers.count("bilinear") > 0);
                        rc.overrideSampler(sets[3][0], samplers["point"]);    // Albedo
                        rc.overrideSampler(sets[3][1], samplers["point"]);    // Normal
                        rc.overrideSampler(sets[3][2], samplers["point"]);    // Emissive
                        rc.overrideSampler(sets[3][3], samplers["point"]);    // MetallicRoughnessAO
                        rc.overrideSampler(sets[3][4], samplers["point"]);    // Depth
                        rc.overrideSampler(sets[3][5], samplers["bilinear"]); // LTCMat
                        rc.overrideSampler(sets[3][6], samplers["bilinear"]); // LTCMag
                        cb.pushConstants(rhi::ShaderStages::eFragment, 0, &pushConstants);
                        rc.bindDescriptorSets(*pipeline);
                        cb.beginRendering(*framebufferInfo).drawFullScreenTriangle();
                        rc.endRendering();
                    }
                });

            add(blackboard, sceneColorData);
        }

        rhi::GraphicsPipeline DeferredLightingPass::createPipeline() const
        {
            return rhi::GraphicsPipeline::Builder {}
                .setColorFormats({rhi::PixelFormat::eRGBA16F, rhi::PixelFormat::eRGBA16F})
                .setInputAssembly({})
                .addBuiltinShader(rhi::ShaderType::eVertex, fullscreen_triangle_vert_spv)
                .addBuiltinShader(rhi::ShaderType::eFragment, deferred_lighting_frag_spv)
                .setDepthStencil({
                    .depthTest  = false,
                    .depthWrite = false,
                })
                .setRasterizer({
                    .polygonMode = rhi::PolygonMode::eFill,
                    .cullMode    = rhi::CullMode::eFront,
                })
                .setBlending(0, {.enabled = false})
                .setBlending(1, {.enabled = false})
                .build(getRenderDevice());
        }
    } // namespace gfx
} // namespace vultra