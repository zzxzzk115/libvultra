#include "vultra/function/rendering/srp/builtin/passes/gaussian_splat_depth_consolidate_pass.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"
#include "vultra/function/rendering/srp/builtin/resource_keys.hpp"

#include <fg/FrameGraph.hpp>

namespace vultra
{
    GaussianSplatDepthConsolidatePass::GaussianSplatDepthConsolidatePass()
    {
        setShaderProfile(rhi::ShaderProfile::eHighend);
    }

    namespace
    {
        constexpr auto PASS_NAME = "GaussianSplatDepthConsolidatePass";
    }

    FrameGraphResource GaussianSplatDepthConsolidatePass::addPass(FrameGraphBuildContext& ctx,
                                                                  FrameGraphResource      depthTransmittance)
    {
        const auto resolution = ctx.view().extent;
        const auto sceneDepth = ctx.data.tryGet(kResKey_DepthTexture);

        struct PassData
        {
            FrameGraphResource depthTransmittance;
            FrameGraphResource sceneDepth;
            FrameGraphResource resolvedDepth;
        };

        auto data = ctx.fg.addCallbackPass<PassData>(
            PASS_NAME,
            [depthTransmittance, sceneDepth, resolution](FrameGraph::Builder& builder, PassData& pd) {
                PASS_SETUP_ZONE;

                pd.depthTransmittance =
                    builder.read(depthTransmittance,
                                 framegraph::TextureRead {
                                     .binding =
                                         {
                                             .location      = {.set = 0, .binding = 0},
                                             .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                         },
                                     .type        = framegraph::TextureRead::Type::eStorageImage,
                                     .imageAspect = rhi::ImageAspect::eColor,
                                 });

                if (sceneDepth)
                {
                    pd.sceneDepth =
                        builder.read(sceneDepth,
                                     framegraph::TextureRead {
                                         .binding =
                                             {
                                                 .location      = {.set = 0, .binding = 1},
                                                 .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                             },
                                         .type        = framegraph::TextureRead::Type::eCombinedImageSampler,
                                         .imageAspect = rhi::ImageAspect::eDepth,
                                     });
                }

                pd.resolvedDepth = builder.create<framegraph::FrameGraphTexture>(
                    "GaussianSplatResolvedDepth",
                    {
                        .extent     = resolution,
                        .format     = rhi::PixelFormat::eDepth32F,
                        .usageFlags = rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled,
                    });
                pd.resolvedDepth = builder.write(pd.resolvedDepth,
                                                 framegraph::Attachment {
                                                     .imageAspect = rhi::ImageAspect::eDepth,
                                                     .clearValue  = framegraph::ClearValue::eOne,
                                                 });
            },
            [this](const PassData& pd, FrameGraphPassResources&, void* ctxPtr) {
                auto& rc = *static_cast<FrameGraphExecContext*>(ctxPtr);
                if (!rc.ext.builtinShaderLib)
                    return;

                setRenderDevice(rc.rd);
                setShaderLib(*rc.ext.builtinShaderLib);

                RHI_GPU_ZONE(rc.cb, PASS_NAME);

                const bool  useSceneDepth = pd.sceneDepth != FrameGraphResource {};
                const auto* pipeline      = getPipeline(useSceneDepth);
                if (!pipeline)
                    return;

                auto samplerIt = rc.ext.samplers.find("nearest");
                if (samplerIt == rc.ext.samplers.end())
                    samplerIt = rc.ext.samplers.find("default");

                if (useSceneDepth && samplerIt != rc.ext.samplers.end())
                    rc.overrideSampler(rc.resourceSet[0][1], samplerIt->second);

                rc.cb.beginRendering(rc.framebufferInfo().value()).bindPipeline(*pipeline);
                rc.bindDescriptorSets(*pipeline);
                rc.cb.drawFullScreenTriangle().endRendering();
                rc.clear();
            });

        ctx.data.set(kResKey_GaussianSplatResolvedDepth, data.resolvedDepth);
        return data.resolvedDepth;
    }

    rhi::GraphicsPipeline GaussianSplatDepthConsolidatePass::createPipeline(bool useSceneDepth) const
    {
        auto vertexShaderVariantHash =
            computeHighendVariantHash("fullscreen_triangle.vert", vshadersystem::ShaderStage::eVert, {});
        auto vertexShader = loadHighendShaderVariant(vertexShaderVariantHash, vshadersystem::ShaderStage::eVert);
        if (!vertexShader)
        {
            VULTRA_CORE_ERROR("[GaussianSplatDepthConsolidatePass] Failed to load vertex shader");
            return {};
        }

        auto fragmentShaderVariantHash =
            computeHighendVariantHash("gaussian_splat_depth_consolidate.frag",
                                              vshadersystem::ShaderStage::eFrag,
                                              {{"USE_SCENE_DEPTH", useSceneDepth ? 1u : 0u}});
        auto fragmentShader = loadHighendShaderVariant(fragmentShaderVariantHash, vshadersystem::ShaderStage::eFrag);
        if (!fragmentShader)
        {
            VULTRA_CORE_ERROR("[GaussianSplatDepthConsolidatePass] Failed to load fragment shader");
            return {};
        }

        return rhi::GraphicsPipeline::Builder {}
            .setColorFormats({})
            .setDepthFormat(rhi::PixelFormat::eDepth32F)
            .setInputAssembly({})
            .addBuiltinShader(rhi::ShaderType::eVertex, vertexShader->spirv)
            .addBuiltinShader(rhi::ShaderType::eFragment, fragmentShader->spirv)
            .setDepthStencil({
                .depthTest      = true,
                .depthWrite     = true,
                .depthCompareOp = rhi::CompareOp::eAlways,
            })
            .setRasterizer({
                .polygonMode = rhi::PolygonMode::eFill,
                .cullMode    = rhi::CullMode::eNone,
            })
            .build(getRenderDevice());
    }
} // namespace vultra
