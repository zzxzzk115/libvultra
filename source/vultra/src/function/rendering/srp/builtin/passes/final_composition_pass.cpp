#include "vultra/function/rendering/srp/builtin/passes/final_composition_pass.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/rendering/srp/builtin/resource_keys.hpp"

#include <fg/FrameGraph.hpp>

namespace vultra
{
    FinalCompositionPass::FinalCompositionPass() { setShaderProfile(rhi::ShaderProfile::eGeneral); }

    namespace
    {
        [[nodiscard]] constexpr bool isSrgbColorFormat(const rhi::PixelFormat format)
        {
            return format == rhi::PixelFormat::eRGBA8_sRGB || format == rhi::PixelFormat::eBGRA8_sRGB;
        }

    } // namespace

    constexpr auto PASS_NAME = "FinalCompositionPass";

    FrameGraphResource FinalCompositionPass::compose(FrameGraphBuildContext& ctx, FrameGraphResource target)
    {
        auto       source       = ctx.data.get(kResKey_FinalCompositionSource);
        auto       entityId     = ctx.data.tryGet(kResKey_GBufferEntityId);
        const bool useMultiview = ctx.view().enableMultiview && ctx.view().multiviewCameraCount == 2u;
        const bool debugEntityIdOutput = ctx.view().camera && ctx.view().camera->debugEntityIdOutput && entityId;

        ctx.fg.addCallbackPass(
            PASS_NAME,
            [source, entityId, debugEntityIdOutput, &target](FrameGraph::Builder& builder, auto&) {
                PASS_SETUP_ZONE;

                builder.read(source,
                             framegraph::TextureRead {
                                 .binding =
                                     {
                                         .location      = {.set = 3, .binding = 0},
                                         .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                     },
                                 .type        = framegraph::TextureRead::Type::eCombinedImageSampler,
                                 .imageAspect = rhi::ImageAspect::eColor,
                             });
                if (debugEntityIdOutput)
                {
                    builder.read(entityId,
                                 framegraph::TextureRead {
                                     .binding =
                                         {
                                             .location      = {.set = 3, .binding = 1},
                                             .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                         },
                                     .type        = framegraph::TextureRead::Type::eCombinedImageSampler,
                                     .imageAspect = rhi::ImageAspect::eColor,
                                 });
                }

                target = builder.write(target,
                                       framegraph::Attachment {
                                           .index       = 0,
                                           .imageAspect = rhi::ImageAspect::eColor,
                                           .clearValue  = framegraph::ClearValue::eOpaqueBlack,
                                       });
            },
            [this, target, useMultiview, debugEntityIdOutput](const auto&, FrameGraphPassResources&, void* ctxPtr) {
                VULTRA_SCOPED_FRAMEGRAPH_EXEC_CONTEXT(rc, ctxPtr);
                setRenderDevice(rc.rd);
                if (!rc.ext.builtinShaderLib)
                {
                    return;
                }
                setShaderLib(*rc.ext.builtinShaderLibForProfile(getShaderProfile()));

                RHI_GPU_ZONE(rc.cb, PASS_NAME);

                assert(rc.framebufferInfo().has_value());
                const auto* pipeline = getPipeline(rhi::getColorFormat(rc.framebufferInfo().value(), 0),
                                                   useMultiview,
                                                   debugEntityIdOutput);
                if (!pipeline)
                {
                    return;
                }

                rc.overrideSampler(rc.resourceSet[3][0], rc.ext.samplers["linear"]);
                if (debugEntityIdOutput)
                    rc.overrideSampler(rc.resourceSet[3][1], rc.ext.samplers["nearest"]);
                rc.cb.bindPipeline(*pipeline);
                rc.bindDescriptorSets(*pipeline);
                auto framebufferInfo = rc.framebufferInfo().value();
                if (!framebufferInfo.colorAttachments.empty())
                {
                    framebufferInfo.colorAttachments[0].clearValue = std::nullopt;
                    framebufferInfo.colorAttachments[0].loadOp     = rhi::AttachmentLoadOp::eDontCare;
                }
                if (useMultiview)
                {
                    framebufferInfo.layers   = 2u;
                    framebufferInfo.viewMask = 0x3u;
                }
                rc.cb.beginRendering(framebufferInfo).drawFullScreenTriangle().endRendering();
            });

        return target;
    }

    rhi::GraphicsPipeline FinalCompositionPass::createPipeline(const rhi::PixelFormat colorFormat,
                                                               const bool             useMultiview,
                                                               const bool             debugEntityIdOutput) const
    {
        auto vertexShader = loadGeneralShader("fullscreen_triangle.vert", vshadersystem::ShaderStage::eVert);
        if (!vertexShader)
        {
            VULTRA_CORE_ERROR("[FinalCompositionPass] Failed to load vertex shader variant");
            return {};
        }

        const bool manualSrgbEncode = !isSrgbColorFormat(colorFormat);
        rhi::ShaderLibraryRuntime::KeywordValues fragmentKeywords {
            {"MANUAL_SRGB_ENCODE", manualSrgbEncode ? 1u : 0u},
            {"USE_MULTIVIEW", useMultiview ? 1u : 0u},
            {"DEBUG_ENTITY_ID_OUTPUT", debugEntityIdOutput ? 1u : 0u},
        };

        auto fragmentShader = loadGeneralShader("final_composition.frag", vshadersystem::ShaderStage::eFrag, fragmentKeywords);
        if (!fragmentShader)
        {
            VULTRA_CORE_ERROR("[FinalCompositionPass] Failed to load fragment shader variant");
            return {};
        }

        auto builder = rhi::GraphicsPipeline::Builder {};
        builder.setViewMask(useMultiview ? 0x3u : 0u)
            .setColorFormats({colorFormat})
            .setInputAssembly({})
            .setDepthStencil({
                .depthTest  = false,
                .depthWrite = false,
            })
            .setRasterizer({
                .polygonMode = rhi::PolygonMode::eFill,
                .cullMode    = rhi::CullMode::eNone,
            })
            .setBlending(0, {.enabled = false});

        const bool webgpu = getRenderDevice().getBackendApi() == rhi::RenderBackendApi::eWebGPU;
        if (webgpu)
        {
            builder
                .addShader(rhi::ShaderType::eVertex,
                           {
                               .code           = vertexShader->wgsl,
                               .entryPointName = "main",
                               .defines        = {},
                               .reflection     = vertexShader->reflection,
                           })
                .addShader(rhi::ShaderType::eFragment,
                           {
                               .code           = fragmentShader->wgsl,
                               .entryPointName = "main",
                               .defines        = {},
                               .reflection     = fragmentShader->reflection,
                           });
        }
        else
        {
            builder.addBuiltinShader(rhi::ShaderType::eVertex, *vertexShader)
                .addBuiltinShader(rhi::ShaderType::eFragment, *fragmentShader);
        }

        return builder.build(getRenderDevice());
    }
} // namespace vultra
