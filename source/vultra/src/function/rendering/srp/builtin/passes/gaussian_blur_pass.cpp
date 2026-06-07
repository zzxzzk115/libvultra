#include "vultra/function/rendering/srp/builtin/passes/gaussian_blur_pass.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/structs/pixel_format.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"
#include "vultra/function/rendering/srp/render_target_desc.hpp"

#include <fg/FrameGraph.hpp>

namespace vultra
{
    GaussianBlurPass::GaussianBlurPass() { setShaderProfile(rhi::ShaderProfile::eGeneral); }

    namespace
    {
        constexpr auto PASS_NAME = "GaussianBlurPass";

        struct PushConstants
        {
            float scale {1.0f};
            int   horizontal {1};
        };
    } // namespace

    FrameGraphResource
    GaussianBlurPass::addPass(FrameGraphBuildContext& ctx, FrameGraphResource source, const float scale, const bool horizontal)
    {
        if (!source)
            return source;

        const auto sourceDesc = ctx.fg.getDescriptor<framegraph::FrameGraphTexture>(source);

        struct PassData
        {
            FrameGraphResource source;
            FrameGraphResource output;
        };

        const auto data = ctx.fg.addCallbackPass<PassData>(
            horizontal ? "GaussianBlurHorizontal" : "GaussianBlurVertical",
            [source, horizontal, outputDesc = makeInheritedTextureDesc(sourceDesc, rhi::PixelFormat::eRGBA16F)](
                FrameGraph::Builder& builder, PassData& pd) {
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

                pd.output = builder.create<framegraph::FrameGraphTexture>(
                    horizontal ? "GaussianBlurH" : "GaussianBlurV", outputDesc);
                pd.output = builder.write(pd.output,
                                          framegraph::Attachment {
                                              .index       = 0,
                                              .imageAspect = rhi::ImageAspect::eColor,
                                              .clearValue  = framegraph::ClearValue::eOpaqueBlack,
                                          });
            },
            [this, scale, horizontal](const PassData&, FrameGraphPassResources&, void* ctxPtr) {
                VULTRA_SCOPED_FRAMEGRAPH_EXEC_CONTEXT(rc, ctxPtr);
                setRenderDevice(rc.rd);
                if (!rc.ext.builtinShaderLib)
                    return;
                setShaderLib(*rc.ext.builtinShaderLibForProfile(getShaderProfile()));

                RHI_GPU_ZONE(rc.cb, PASS_NAME);

                assert(rc.framebufferInfo().has_value());
                const auto framebufferInfo = rc.framebufferInfo().value();
                const auto* pipeline = getPipeline(rhi::getColorFormat(framebufferInfo, 0), framebufferInfo.viewMask);
                if (!pipeline)
                    return;

                const PushConstants pc {
                    .scale      = scale,
                    .horizontal = horizontal ? 1 : 0,
                };

                rc.overrideSampler(rc.resourceSet[3][0], rc.ext.samplers["bilinear"]);
                rc.cb.bindPipeline(*pipeline);
                rc.bindDescriptorSets(*pipeline);
                rc.cb.pushConstants(rhi::ShaderStages::eFragment, 0, &pc);
                rc.cb.beginRendering(rc.framebufferInfo().value()).drawFullScreenTriangle().endRendering();
            });

        return data.output;
    }

    FrameGraphResource GaussianBlurPass::addPass(FrameGraphBuildContext& ctx, FrameGraphResource source, const float scale)
    {
        const auto horizontal = addPass(ctx, source, scale, true);
        return addPass(ctx, horizontal, scale, false);
    }

    rhi::GraphicsPipeline GaussianBlurPass::createPipeline(const rhi::PixelFormat colorFormat, const uint32_t viewMask) const
    {
        auto vertexShader = loadGeneralShader("fullscreen_triangle.vert", vshadersystem::ShaderStage::eVert);
        if (!vertexShader)
        {
            VULTRA_CORE_ERROR("[GaussianBlurPass] Failed to load vertex shader");
            return {};
        }

        rhi::ShaderLibraryRuntime::KeywordValues fragmentKeywords {
            {"USE_MULTIVIEW", viewMask != 0u ? 1u : 0u},
        };
        auto fragmentShader = loadGeneralShader("gaussian_blur.frag", vshadersystem::ShaderStage::eFrag, fragmentKeywords);
        if (!fragmentShader)
        {
            VULTRA_CORE_ERROR("[GaussianBlurPass] Failed to load fragment shader");
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
