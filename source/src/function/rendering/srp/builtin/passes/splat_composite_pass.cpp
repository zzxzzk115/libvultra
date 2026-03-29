#include "vultra/function/rendering/srp/builtin/passes/splat_composite_pass.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"

#include <fg/FrameGraph.hpp>

namespace vultra
{
    namespace
    {
        constexpr auto PASS_NAME = "SplatCompositePass";
    }

    FrameGraphResource SplatCompositePass::addPass(FrameGraphBuildContext& ctx,
                                                   FrameGraphResource      meshletColor,
                                                   FrameGraphResource      splatColor,
                                                   FrameGraphResource      splatDepthAccum,
                                                   FrameGraphResource      sceneDepth)
    {
        const auto resolution = ctx.view().extent;

        struct PassData
        {
            FrameGraphResource meshlet;
            FrameGraphResource splat;
            FrameGraphResource splatDepthAccum;
            FrameGraphResource sceneDepth;
            FrameGraphResource color;
            bool               useDepthAware {false};
        };

        auto data = ctx.fg.addCallbackPass<PassData>(
            PASS_NAME,
            [meshletColor, splatColor, splatDepthAccum, sceneDepth, resolution](FrameGraph::Builder& builder,
                                                                                PassData&            pd) {
                PASS_SETUP_ZONE;

                pd.meshlet = builder.read(meshletColor,
                                          framegraph::TextureRead {
                                              .binding =
                                                  {
                                                      .location      = {.set = 3, .binding = 0},
                                                      .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                                  },
                                              .type        = framegraph::TextureRead::Type::eCombinedImageSampler,
                                              .imageAspect = rhi::ImageAspect::eColor,
                                          });

                pd.splat = builder.read(splatColor,
                                        framegraph::TextureRead {
                                            .binding =
                                                {
                                                    .location      = {.set = 3, .binding = 1},
                                                    .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                                },
                                            .type        = framegraph::TextureRead::Type::eCombinedImageSampler,
                                            .imageAspect = rhi::ImageAspect::eColor,
                                        });

                pd.useDepthAware = splatDepthAccum && sceneDepth;
                if (pd.useDepthAware)
                {
                    pd.splatDepthAccum =
                        builder.read(splatDepthAccum,
                                     framegraph::TextureRead {
                                         .binding =
                                             {
                                                 .location      = {.set = 3, .binding = 2},
                                                 .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                             },
                                         .type        = framegraph::TextureRead::Type::eCombinedImageSampler,
                                         .imageAspect = rhi::ImageAspect::eColor,
                                     });

                    pd.sceneDepth =
                        builder.read(sceneDepth,
                                     framegraph::TextureRead {
                                         .binding =
                                             {
                                                 .location      = {.set = 3, .binding = 3},
                                                 .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                             },
                                         .type        = framegraph::TextureRead::Type::eCombinedImageSampler,
                                         .imageAspect = rhi::ImageAspect::eDepth,
                                     });
                }

                pd.color = builder.create<framegraph::FrameGraphTexture>(
                    "SplatComposite",
                    {
                        .extent     = resolution,
                        .format     = rhi::PixelFormat::eRGBA8_UNorm,
                        .usageFlags = rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled,
                    });
                pd.color = builder.write(pd.color,
                                         framegraph::Attachment {
                                             .index       = 0,
                                             .imageAspect = rhi::ImageAspect::eColor,
                                             .clearValue  = framegraph::ClearValue::eOpaqueBlack,
                                         });
            },
            [this](const PassData& pd, FrameGraphPassResources&, void* ctxPtr) {
                auto& rc = *static_cast<FrameGraphExecContext*>(ctxPtr);
                setRenderDevice(rc.rd);
                if (!rc.ext.builtinShaderLib)
                    return;
                setShaderLib(*rc.ext.builtinShaderLib);

                RHI_GPU_ZONE(rc.cb, PASS_NAME);

                assert(rc.framebufferInfo().has_value());
                const bool  useDepthAware = pd.useDepthAware;
                const auto* pipeline = getPipeline(rhi::getColorFormat(rc.framebufferInfo().value(), 0), useDepthAware);
                if (!pipeline)
                    return;

                // Prefer linear filtering, then fallback to default/nearest.
                auto samplerIt = rc.ext.samplers.find("linear");
                if (samplerIt == rc.ext.samplers.end())
                    samplerIt = rc.ext.samplers.find("default");
                if (samplerIt == rc.ext.samplers.end())
                    samplerIt = rc.ext.samplers.find("nearest");

                if (samplerIt != rc.ext.samplers.end())
                {
                    rc.overrideSampler(rc.resourceSet[3][0], samplerIt->second);
                    rc.overrideSampler(rc.resourceSet[3][1], samplerIt->second);
                    if (useDepthAware)
                    {
                        rc.overrideSampler(rc.resourceSet[3][2], samplerIt->second);
                        rc.overrideSampler(rc.resourceSet[3][3], samplerIt->second);
                    }
                }

                rc.cb.beginRendering(rc.framebufferInfo().value()).bindPipeline(*pipeline);
                rc.bindDescriptorSets(*pipeline);
                rc.cb.drawFullScreenTriangle().endRendering();
                rc.clear();
            });

        return data.color;
    }

    rhi::GraphicsPipeline SplatCompositePass::createPipeline(rhi::PixelFormat colorFormat, bool useDepthAware) const
    {
        auto vertexShaderVariantHash =
            getShaderLib().computeVariantHash("fullscreen_triangle.vert", vshadersystem::ShaderStage::eVert, {});
        auto vertexShader = getShaderLib().load(vertexShaderVariantHash, vshadersystem::ShaderStage::eVert);
        if (!vertexShader)
        {
            VULTRA_CORE_ERROR("[SplatCompositePass] Failed to load vertex shader");
            return {};
        }

        auto fragmentShaderVariantHash = getShaderLib().computeVariantHash(
            "splat_composite.frag", vshadersystem::ShaderStage::eFrag, {{"USE_DEPTH_AWARE", useDepthAware ? 1u : 0u}});
        auto fragmentShader = getShaderLib().load(fragmentShaderVariantHash, vshadersystem::ShaderStage::eFrag);
        if (!fragmentShader)
        {
            VULTRA_CORE_ERROR("[SplatCompositePass] Failed to load fragment shader");
            return {};
        }

        return rhi::GraphicsPipeline::Builder {}
            .setColorFormats({colorFormat})
            .setInputAssembly({})
            .addBuiltinShader(rhi::ShaderType::eVertex, vertexShader->spirv)
            .addBuiltinShader(rhi::ShaderType::eFragment, fragmentShader->spirv)
            .setDepthStencil({.depthTest = false, .depthWrite = false})
            .setRasterizer({.polygonMode = rhi::PolygonMode::eFill, .cullMode = rhi::CullMode::eFront})
            .setBlending(0, {.enabled = false})
            .build(getRenderDevice());
    }
} // namespace vultra
