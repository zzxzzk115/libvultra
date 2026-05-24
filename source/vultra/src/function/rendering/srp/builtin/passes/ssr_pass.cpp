#include "vultra/function/rendering/srp/builtin/passes/ssr_pass.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/structs/pixel_format.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"

#include <fg/FrameGraph.hpp>

namespace vultra
{
    SsrPass::SsrPass() { setShaderProfile(rhi::ShaderProfile::eHighend); }

    namespace
    {
        constexpr auto PASS_NAME = "SSRPass";

        struct SsrPushConstants
        {
            float   reflectionFactor {1.0f};
            int32_t maxSteps {32};
            int32_t binaryRefinement {6};
            float   stride {0.1f};
            float   thickness {1.0f};
        };
    } // namespace

    FrameGraphResource SsrPass::addPass(FrameGraphBuildContext&  ctx,
                                        FrameGraphResource       color,
                                        FrameGraphResource       depth,
                                        FrameGraphResource       normal,
                                        FrameGraphResource       material,
                                        const SsrRenderSettings& settings)
    {
        const auto resolution  = ctx.view().extent;
        const auto cameraBlock = ctx.bb.get<CameraData>().cameraBlock.fgResource;

        struct PassData
        {
            FrameGraphResource camera;
            FrameGraphResource color;
            FrameGraphResource depth;
            FrameGraphResource normal;
            FrameGraphResource material;
            FrameGraphResource output;
        };

        const auto data = ctx.fg.addCallbackPass<PassData>(
            PASS_NAME,
            [cameraBlock, color, depth, normal, material, resolution](FrameGraph::Builder& builder, PassData& pd) {
                PASS_SETUP_ZONE;

                pd.camera = builder.read(cameraBlock,
                                         framegraph::BindingInfo {
                                             .location      = {.set = 0, .binding = 0},
                                             .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                         });
                pd.color = builder.read(color,
                                        framegraph::TextureRead {
                                            .binding =
                                                {
                                                    .location      = {.set = 3, .binding = 0},
                                                    .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                                },
                                            .type        = framegraph::TextureRead::Type::eCombinedImageSampler,
                                            .imageAspect = rhi::ImageAspect::eColor,
                                        });
                pd.depth = builder.read(depth,
                                        framegraph::TextureRead {
                                            .binding =
                                                {
                                                    .location      = {.set = 3, .binding = 1},
                                                    .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                                },
                                            .type        = framegraph::TextureRead::Type::eCombinedImageSampler,
                                            .imageAspect = rhi::ImageAspect::eDepth,
                                        });
                pd.normal = builder.read(normal,
                                         framegraph::TextureRead {
                                             .binding =
                                                 {
                                                     .location      = {.set = 3, .binding = 2},
                                                     .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                                 },
                                             .type        = framegraph::TextureRead::Type::eCombinedImageSampler,
                                             .imageAspect = rhi::ImageAspect::eColor,
                                         });
                pd.material = builder.read(material,
                                           framegraph::TextureRead {
                                               .binding =
                                                   {
                                                       .location      = {.set = 3, .binding = 3},
                                                       .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                                   },
                                               .type        = framegraph::TextureRead::Type::eCombinedImageSampler,
                                               .imageAspect = rhi::ImageAspect::eColor,
                                           });

                pd.output = builder.create<framegraph::FrameGraphTexture>(
                    "SSR",
                    {
                        .extent     = resolution,
                        .format     = rhi::PixelFormat::eRGBA8_UNorm,
                        .usageFlags = rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled |
                                      rhi::ImageUsage::eTransferSrc,
                    });
                pd.output = builder.write(pd.output,
                                          framegraph::Attachment {
                                              .index       = 0,
                                              .imageAspect = rhi::ImageAspect::eColor,
                                              .clearValue  = framegraph::ClearValue::eTransparentBlack,
                                          });
            },
            [this, settings](const PassData&, FrameGraphPassResources&, void* ctxPtr) {
                VULTRA_SCOPED_FRAMEGRAPH_EXEC_CONTEXT(rc, ctxPtr);
                setRenderDevice(rc.rd);
                if (!rc.ext.builtinShaderLib)
                    return;
                setShaderLib(*rc.ext.builtinShaderLib);

                RHI_GPU_ZONE(rc.cb, PASS_NAME);

                assert(rc.framebufferInfo().has_value());
                const auto* pipeline = getPipeline(rhi::getColorFormat(rc.framebufferInfo().value(), 0));
                if (!pipeline)
                    return;

                SsrPushConstants pc {
                    .reflectionFactor = settings.enabled ? settings.reflectionFactor : 0.0f,
                    .maxSteps         = settings.maxSteps,
                    .binaryRefinement = settings.binaryRefinement,
                    .stride           = settings.stride,
                    .thickness        = settings.thickness,
                };

                rc.overrideSampler(rc.resourceSet[3][0], rc.ext.samplers["bilinear"]);
                rc.overrideSampler(rc.resourceSet[3][1], rc.ext.samplers["nearest"]);
                rc.overrideSampler(rc.resourceSet[3][2], rc.ext.samplers["nearest"]);
                rc.overrideSampler(rc.resourceSet[3][3], rc.ext.samplers["nearest"]);
                rc.cb.bindPipeline(*pipeline);
                rc.bindDescriptorSets(*pipeline);
                rc.cb.pushConstants(rhi::ShaderStages::eFragment, 0, &pc);
                rc.cb.beginRendering(rc.framebufferInfo().value()).drawFullScreenTriangle().endRendering();
            });

        return data.output;
    }

    rhi::GraphicsPipeline SsrPass::createPipeline(const rhi::PixelFormat colorFormat) const
    {
        auto vertexShader = loadHighendShader("fullscreen_triangle.vert", vshadersystem::ShaderStage::eVert);
        if (!vertexShader)
        {
            VULTRA_CORE_ERROR("[SSRPass] Failed to load vertex shader");
            return {};
        }

        auto fragmentShader = loadHighendShader("ssr.frag", vshadersystem::ShaderStage::eFrag);
        if (!fragmentShader)
        {
            VULTRA_CORE_ERROR("[SSRPass] Failed to load fragment shader");
            return {};
        }

        return rhi::GraphicsPipeline::Builder {}
            .setColorFormats({colorFormat})
            .setInputAssembly({})
            .addBuiltinShader(rhi::ShaderType::eVertex, *vertexShader)
            .addBuiltinShader(rhi::ShaderType::eFragment, *fragmentShader)
            .setDepthStencil({
                .depthTest  = false,
                .depthWrite = false,
            })
            .setRasterizer({
                .polygonMode = rhi::PolygonMode::eFill,
                .cullMode    = rhi::CullMode::eNone,
            })
            .setBlending(0, {.enabled = false})
            .build(getRenderDevice());
    }
} // namespace vultra
