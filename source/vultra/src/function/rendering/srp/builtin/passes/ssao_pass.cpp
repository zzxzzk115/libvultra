#include "vultra/function/rendering/srp/builtin/passes/ssao_pass.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/structs/pixel_format.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"
#include "vultra/function/rendering/srp/render_target_desc.hpp"

#include <fg/FrameGraph.hpp>
#include <format>

namespace vultra
{
    SsaoPass::SsaoPass() { setShaderProfile(rhi::ShaderProfile::eHighend); }

    namespace
    {
        constexpr auto PASS_NAME = "SSAOPass";

        struct SsaoPushConstants
        {
            float   radius {1.5f};
            float   bias {0.05f};
            float   intensity {1.2f};
            int32_t maxRadiusPixels {32};
            int32_t stepCount {4};
            int32_t directionCount {8};
        };
    } // namespace

    FrameGraphResource SsaoPass::addPass(FrameGraphBuildContext&     ctx,
                                         FrameGraphResource          depth,
                                         FrameGraphResource          normal,
                                         const SsaoRenderSettings&   settings)
    {
        const auto depthDesc   = ctx.fg.getDescriptor<framegraph::FrameGraphTexture>(depth);
        const auto outputDesc  = makeInheritedTextureDesc(depthDesc, rhi::PixelFormat::eR8_UNorm);
        const auto resolution  = depthDesc.extent;
        const auto cameraBlock = ctx.bb.get<CameraData>().cameraBlock.fgResource;

        struct PassData
        {
            FrameGraphResource camera;
            FrameGraphResource depth;
            FrameGraphResource normal;
            FrameGraphResource output;
        };

        const auto data = ctx.fg.addCallbackPass<PassData>(
            PASS_NAME,
            [cameraBlock, depth, normal, outputDesc](FrameGraph::Builder& builder, PassData& pd) {
                PASS_SETUP_ZONE;

                pd.camera = builder.read(cameraBlock,
                                         framegraph::BindingInfo {
                                             .location      = {.set = 0, .binding = 0},
                                             .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                         });
                pd.depth = builder.read(depth,
                                        framegraph::TextureRead {
                                            .binding =
                                                {
                                                    .location      = {.set = 3, .binding = 0},
                                                    .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                                },
                                            .type        = framegraph::TextureRead::Type::eCombinedImageSampler,
                                            .imageAspect = rhi::ImageAspect::eDepth,
                                        });
                pd.normal = builder.read(normal,
                                         framegraph::TextureRead {
                                             .binding =
                                                 {
                                                     .location      = {.set = 3, .binding = 1},
                                                     .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                                 },
                                             .type        = framegraph::TextureRead::Type::eCombinedImageSampler,
                                             .imageAspect = rhi::ImageAspect::eColor,
                                         });

                pd.output = builder.create<framegraph::FrameGraphTexture>(
                    "SSAO",
                    outputDesc);
                pd.output = builder.write(pd.output,
                                          framegraph::Attachment {
                                              .index       = 0,
                                              .imageAspect = rhi::ImageAspect::eColor,
                                              .clearValue  = framegraph::ClearValue::eOpaqueWhite,
                                          });
            },
            [this, settings, resolution](const PassData&, FrameGraphPassResources&, void* ctxPtr) {
                VULTRA_SCOPED_FRAMEGRAPH_EXEC_CONTEXT(rc, ctxPtr);
                setRenderDevice(rc.rd);
                if (!rc.ext.builtinShaderLib)
                    return;
                setShaderLib(*rc.ext.builtinShaderLib);

                assert(rc.framebufferInfo().has_value());
                const auto framebufferInfo = rc.framebufferInfo().value();
                const auto* pipeline = getPipeline(rhi::getColorFormat(framebufferInfo, 0), framebufferInfo.viewMask);
                if (!pipeline)
                    return;

                SsaoPushConstants pc {
                    .radius          = settings.radius,
                    .bias            = settings.bias,
                    .intensity       = settings.enabled ? settings.intensity : 0.0f,
                    .maxRadiusPixels = settings.maxRadiusPixels,
                    .stepCount       = settings.stepCount,
                    .directionCount  = settings.directionCount,
                };

                rc.overrideSampler(rc.resourceSet[3][0], rc.ext.samplers["nearest"]);
                rc.overrideSampler(rc.resourceSet[3][1], rc.ext.samplers["nearest"]);
                rc.cb.bindPipeline(*pipeline);
                rc.bindDescriptorSets(*pipeline);
                rc.cb.pushConstants(rhi::ShaderStages::eFragment, 0, &pc);
                rc.cb.beginRendering(framebufferInfo);
                {
                    const auto scopeName = std::format("{} {}x{}", PASS_NAME, resolution.width, resolution.height);
                    RHI_GPU_ZONE(rc.cb, scopeName.c_str());
                    rc.cb.drawFullScreenTriangle();
                }
                rc.cb.endRendering();
            });

        return data.output;
    }

    rhi::GraphicsPipeline SsaoPass::createPipeline(const rhi::PixelFormat colorFormat, const uint32_t viewMask) const
    {
        auto vertexShader = loadHighendShader("fullscreen_triangle.vert", vshadersystem::ShaderStage::eVert);
        if (!vertexShader)
        {
            VULTRA_CORE_ERROR("[SSAOPass] Failed to load vertex shader");
            return {};
        }

        rhi::ShaderLibraryRuntime::KeywordValues fragmentKeywords {
            {"USE_MULTIVIEW", viewMask != 0u ? 1u : 0u},
        };
        auto fragmentShader = loadHighendShader("ssao.frag", vshadersystem::ShaderStage::eFrag, fragmentKeywords);
        if (!fragmentShader)
        {
            VULTRA_CORE_ERROR("[SSAOPass] Failed to load fragment shader");
            return {};
        }

        return rhi::GraphicsPipeline::Builder {}
            .setColorFormats({colorFormat})
            .setViewMask(viewMask)
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
