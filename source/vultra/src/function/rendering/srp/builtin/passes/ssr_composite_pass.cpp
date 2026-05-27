#include "vultra/function/rendering/srp/builtin/passes/ssr_composite_pass.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/structs/pixel_format.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"
#include "vultra/function/rendering/srp/render_target_desc.hpp"

#include <fg/FrameGraph.hpp>

namespace vultra
{
    SsrCompositePass::SsrCompositePass() { setShaderProfile(rhi::ShaderProfile::eGeneral); }

    namespace
    {
        constexpr auto PASS_NAME = "SSRCompositePass";
    } // namespace

    FrameGraphResource SsrCompositePass::addPass(FrameGraphBuildContext& ctx,
                                                 FrameGraphResource      source,
                                                 FrameGraphResource      reflection)
    {
        const auto sourceDesc = ctx.fg.getDescriptor<framegraph::FrameGraphTexture>(source);

        struct PassData
        {
            FrameGraphResource source;
            FrameGraphResource reflection;
            FrameGraphResource output;
        };

        const auto data = ctx.fg.addCallbackPass<PassData>(
            PASS_NAME,
            [source, reflection, outputDesc = makeInheritedTextureDesc(sourceDesc, rhi::PixelFormat::eRGBA16F)](
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
                pd.reflection = builder.read(reflection,
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
                    "SSRCompositeOutput",
                    outputDesc);
                pd.output = builder.write(pd.output,
                                          framegraph::Attachment {
                                              .index       = 0,
                                              .imageAspect = rhi::ImageAspect::eColor,
                                              .clearValue  = framegraph::ClearValue::eOpaqueBlack,
                                          });
            },
            [this](const PassData&, FrameGraphPassResources&, void* ctxPtr) {
                VULTRA_SCOPED_FRAMEGRAPH_EXEC_CONTEXT(rc, ctxPtr);
                setRenderDevice(rc.rd);
                if (!rc.ext.builtinShaderLib)
                    return;
                setShaderLib(*rc.ext.builtinShaderLib);

                RHI_GPU_ZONE(rc.cb, PASS_NAME);

                assert(rc.framebufferInfo().has_value());
                const auto framebufferInfo = rc.framebufferInfo().value();
                const auto* pipeline = getPipeline(rhi::getColorFormat(framebufferInfo, 0), framebufferInfo.viewMask);
                if (!pipeline)
                    return;

                rc.overrideSampler(rc.resourceSet[3][0], rc.ext.samplers["bilinear"]);
                rc.overrideSampler(rc.resourceSet[3][1], rc.ext.samplers["bilinear"]);
                rc.cb.bindPipeline(*pipeline);
                rc.bindDescriptorSets(*pipeline);
                rc.cb.beginRendering(rc.framebufferInfo().value()).drawFullScreenTriangle().endRendering();
            });

        return data.output;
    }

    rhi::GraphicsPipeline SsrCompositePass::createPipeline(const rhi::PixelFormat colorFormat, const uint32_t viewMask) const
    {
        auto vertexShader = loadGeneralShader("fullscreen_triangle.vert", vshadersystem::ShaderStage::eVert);
        if (!vertexShader)
        {
            VULTRA_CORE_ERROR("[SSRCompositePass] Failed to load vertex shader");
            return {};
        }

        rhi::ShaderLibraryRuntime::KeywordValues fragmentKeywords {
            {"USE_MULTIVIEW", viewMask != 0u ? 1u : 0u},
        };
        auto fragmentShader = loadGeneralShader("ssr_composite.frag", vshadersystem::ShaderStage::eFrag, fragmentKeywords);
        if (!fragmentShader)
        {
            VULTRA_CORE_ERROR("[SSRCompositePass] Failed to load fragment shader");
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
