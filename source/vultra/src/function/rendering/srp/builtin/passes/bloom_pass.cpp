#include "vultra/function/rendering/srp/builtin/passes/bloom_pass.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/structs/pixel_format.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"
#include "vultra/function/rendering/srp/render_target_desc.hpp"

#include <algorithm>
#include <fg/FrameGraph.hpp>

namespace vultra
{
    BloomPass::BloomPass() { setShaderProfile(rhi::ShaderProfile::eGeneral); }

    namespace
    {
        constexpr auto PASS_NAME = "BloomPass";

        constexpr uint32_t kStagePrefilter = 0u;
        constexpr uint32_t kStageCombine   = 1u;

        struct PrefilterPushConstants
        {
            float threshold {1.0f};
            float knee {0.5f};
        };

        struct CombinePushConstants
        {
            float intensity {0.6f};
        };
    } // namespace

    FrameGraphResource BloomPass::addPass(FrameGraphBuildContext& ctx,
                                          FrameGraphResource      source,
                                          const float             threshold,
                                          const float             knee,
                                          const float             intensity,
                                          const float             blurScale,
                                          const int               iterations)
    {
        if (!source)
            return source;

        const auto sourceDesc = ctx.fg.getDescriptor<framegraph::FrameGraphTexture>(source);

        // ---- 1) Bright/threshold extraction -------------------------------------------------
        struct PrefilterData
        {
            FrameGraphResource source;
            FrameGraphResource output;
        };
        auto prefilter = ctx.fg.addCallbackPass<PrefilterData>(
            "BloomPrefilter",
            [source, outputDesc = makeInheritedTextureDesc(sourceDesc, rhi::PixelFormat::eRGBA16F)](
                FrameGraph::Builder& builder, PrefilterData& pd) {
                PASS_SETUP_ZONE;

                pd.source = builder.read(source,
                                         framegraph::TextureRead {
                                             .binding =
                                                 {
                                                     .location      = {.set = 3, .binding = 0},
                                                     .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                                 },
                                             .type        = framegraph::TextureRead::Type::eCombinedImageSampler,
                                             .imageAspect = rhi::ImageAspect::eColor,
                                         });

                pd.output = builder.create<framegraph::FrameGraphTexture>("BloomBright", outputDesc);
                pd.output = builder.write(pd.output,
                                          framegraph::Attachment {
                                              .index       = 0,
                                              .imageAspect = rhi::ImageAspect::eColor,
                                              .clearValue  = framegraph::ClearValue::eOpaqueBlack,
                                          });
            },
            [this, threshold, knee](const PrefilterData&, FrameGraphPassResources&, void* ctxPtr) {
                VULTRA_SCOPED_FRAMEGRAPH_EXEC_CONTEXT(rc, ctxPtr);
                setRenderDevice(rc.rd);
                if (!rc.ext.builtinShaderLib)
                    return;
                setShaderLib(*rc.ext.builtinShaderLibForProfile(getShaderProfile()));

                RHI_GPU_ZONE(rc.cb, "BloomPrefilter");

                assert(rc.framebufferInfo().has_value());
                const auto framebufferInfo = rc.framebufferInfo().value();
                const auto* pipeline =
                    getPipeline(rhi::getColorFormat(framebufferInfo, 0), framebufferInfo.viewMask, kStagePrefilter);
                if (!pipeline)
                    return;

                const PrefilterPushConstants pc {.threshold = threshold, .knee = knee};

                rc.overrideSampler(rc.resourceSet[3][0], rc.ext.samplers["bilinear"]);
                rc.cb.bindPipeline(*pipeline);
                rc.bindDescriptorSets(*pipeline);
                rc.cb.pushConstants(rhi::ShaderStages::eFragment, 0, &pc);
                rc.cb.beginRendering(framebufferInfo).drawFullScreenTriangle().endRendering();
            });

        // ---- 2) Separable Gaussian blur (ping-pong) -----------------------------------------
        FrameGraphResource bloom       = prefilter.output;
        const int          blurPasses  = std::max(iterations, 1);
        for (int i = 0; i < blurPasses; ++i)
            bloom = m_Blur.addPass(ctx, bloom, blurScale);

        // ---- 3) Additive combine ------------------------------------------------------------
        struct CombineData
        {
            FrameGraphResource source;
            FrameGraphResource bloom;
            FrameGraphResource output;
        };
        auto combine = ctx.fg.addCallbackPass<CombineData>(
            PASS_NAME,
            [source, bloom, outputDesc = makeInheritedTextureDesc(sourceDesc, rhi::PixelFormat::eRGBA16F)](
                FrameGraph::Builder& builder, CombineData& pd) {
                PASS_SETUP_ZONE;

                pd.source = builder.read(source,
                                         framegraph::TextureRead {
                                             .binding =
                                                 {
                                                     .location      = {.set = 3, .binding = 0},
                                                     .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                                 },
                                             .type        = framegraph::TextureRead::Type::eCombinedImageSampler,
                                             .imageAspect = rhi::ImageAspect::eColor,
                                         });
                pd.bloom = builder.read(bloom,
                                        framegraph::TextureRead {
                                            .binding =
                                                {
                                                    .location      = {.set = 3, .binding = 1},
                                                    .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                                },
                                            .type        = framegraph::TextureRead::Type::eCombinedImageSampler,
                                            .imageAspect = rhi::ImageAspect::eColor,
                                        });

                pd.output = builder.create<framegraph::FrameGraphTexture>("BloomOutput", outputDesc);
                pd.output = builder.write(pd.output,
                                          framegraph::Attachment {
                                              .index       = 0,
                                              .imageAspect = rhi::ImageAspect::eColor,
                                              .clearValue  = framegraph::ClearValue::eOpaqueBlack,
                                          });
            },
            [this, intensity](const CombineData&, FrameGraphPassResources&, void* ctxPtr) {
                VULTRA_SCOPED_FRAMEGRAPH_EXEC_CONTEXT(rc, ctxPtr);
                setRenderDevice(rc.rd);
                if (!rc.ext.builtinShaderLib)
                    return;
                setShaderLib(*rc.ext.builtinShaderLibForProfile(getShaderProfile()));

                RHI_GPU_ZONE(rc.cb, PASS_NAME);

                assert(rc.framebufferInfo().has_value());
                const auto framebufferInfo = rc.framebufferInfo().value();
                const auto* pipeline =
                    getPipeline(rhi::getColorFormat(framebufferInfo, 0), framebufferInfo.viewMask, kStageCombine);
                if (!pipeline)
                    return;

                const CombinePushConstants pc {.intensity = intensity};

                rc.overrideSampler(rc.resourceSet[3][0], rc.ext.samplers["bilinear"]);
                rc.overrideSampler(rc.resourceSet[3][1], rc.ext.samplers["bilinear"]);
                rc.cb.bindPipeline(*pipeline);
                rc.bindDescriptorSets(*pipeline);
                rc.cb.pushConstants(rhi::ShaderStages::eFragment, 0, &pc);
                rc.cb.beginRendering(framebufferInfo).drawFullScreenTriangle().endRendering();
            });

        return combine.output;
    }

    rhi::GraphicsPipeline
    BloomPass::createPipeline(const rhi::PixelFormat colorFormat, const uint32_t viewMask, const uint32_t stage) const
    {
        auto vertexShader = loadGeneralShader("fullscreen_triangle.vert", vshadersystem::ShaderStage::eVert);
        if (!vertexShader)
        {
            VULTRA_CORE_ERROR("[BloomPass] Failed to load vertex shader");
            return {};
        }

        rhi::ShaderLibraryRuntime::KeywordValues fragmentKeywords {
            {"USE_MULTIVIEW", viewMask != 0u ? 1u : 0u},
        };
        const auto shaderName = stage == kStagePrefilter ? "bloom_prefilter.frag" : "bloom_combine.frag";
        auto       fragmentShader = loadGeneralShader(shaderName, vshadersystem::ShaderStage::eFrag, fragmentKeywords);
        if (!fragmentShader)
        {
            VULTRA_CORE_ERROR("[BloomPass] Failed to load fragment shader '{}'", shaderName);
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
