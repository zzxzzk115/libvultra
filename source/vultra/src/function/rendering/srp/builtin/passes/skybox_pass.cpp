#include "vultra/function/rendering/srp/builtin/passes/skybox_pass.hpp"

#include "vultra/core/rhi/util.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/rendering/framework/prepared_render_data.hpp"

#include <fg/FrameGraph.hpp>

namespace vultra
{
    SkyboxPass::SkyboxPass() { setShaderProfile(rhi::ShaderProfile::eHighend); }

    namespace
    {
        constexpr auto PASS_NAME = "SkyboxPass";
    }

    FrameGraphResource SkyboxPass::addPass(FrameGraphBuildContext& ctx,
                                           FrameGraphResource      color,
                                           FrameGraphResource      depth,
                                           FrameGraphResource      environmentMap,
                                           rhi::Texture*           cubemapOverride)
    {
        const auto cameraBlock = ctx.bb.get<CameraData>().cameraBlock.fgResource;

        ctx.fg.addCallbackPass(
            PASS_NAME,
            [cameraBlock, depth, environmentMap, &color](FrameGraph::Builder& builder, auto&) {
                PASS_SETUP_ZONE;

                builder.read(cameraBlock,
                             framegraph::BindingInfo {
                                 .location      = {.set = 0, .binding = 0},
                                 .pipelineStage = framegraph::PipelineStage::eVertexShader,
                             });
                builder.read(depth,
                             framegraph::Attachment {
                                 .imageAspect = rhi::ImageAspect::eDepth,
                             });
                builder.read(environmentMap,
                             framegraph::TextureRead {
                                 .binding =
                                     {
                                         .location      = {.set = 3, .binding = 0},
                                         .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                     },
                                 .type        = framegraph::TextureRead::Type::eCombinedImageSampler,
                                 .imageAspect = rhi::ImageAspect::eColor,
                             });
                color = builder.write(color,
                                      framegraph::Attachment {
                                          .index       = 0,
                                          .imageAspect = rhi::ImageAspect::eColor,
                                      });
            },
            [this, cubemapOverride](const auto&, FrameGraphPassResources&, void* ctxPtr) {
                VULTRA_SCOPED_FRAMEGRAPH_EXEC_CONTEXT(rc, ctxPtr);
                setRenderDevice(rc.rd);
                if (!rc.ext.builtinShaderLib)
                    return;
                setShaderLib(*rc.ext.builtinShaderLibForProfile(getShaderProfile()));

                assert(rc.framebufferInfo().has_value());
                const auto framebufferInfo = rc.framebufferInfo().value();
                const auto* pipeline = getPipeline(rhi::getDepthFormat(framebufferInfo),
                                                   rhi::getColorFormat(framebufferInfo, 0),
                                                   framebufferInfo.viewMask);
                if (!pipeline)
                    return;

                RHI_GPU_ZONE(rc.cb, PASS_NAME);
                if (cubemapOverride && *cubemapOverride)
                {
                    rhi::prepareForReading(rc.cb, *cubemapOverride);
                    rc.resourceSet[3][0] = rhi::bindings::CombinedImageSampler {
                        .texture = cubemapOverride,
                        .sampler = rc.ext.samplers["linear"],
                    };
                }
                else
                {
                    rc.overrideSampler(rc.resourceSet[3][0], rc.ext.samplers["linear"]);
                }
                rc.cb.bindPipeline(*pipeline);
                rc.bindDescriptorSets(*pipeline);
                rc.cb.beginRendering(framebufferInfo).drawFullScreenTriangle().endRendering();
            });

        return color;
    }

    rhi::GraphicsPipeline SkyboxPass::createPipeline(const rhi::PixelFormat depthFormat,
                                                     const rhi::PixelFormat colorFormat,
                                                     const uint32_t         viewMask) const
    {
        auto vertexShader = loadHighendShader("skybox.vert", vshadersystem::ShaderStage::eVert);
        auto fragmentShader = loadHighendShader("skybox.frag", vshadersystem::ShaderStage::eFrag);
        if (!vertexShader || !fragmentShader)
        {
            VULTRA_CORE_ERROR("[SkyboxPass] Failed to load shaders");
            return {};
        }

        return rhi::GraphicsPipeline::Builder {}
            .setDepthFormat(depthFormat)
            .setColorFormats({colorFormat})
            .setViewMask(viewMask)
            .setInputAssembly({})
            .addBuiltinShader(rhi::ShaderType::eVertex, *vertexShader)
            .addBuiltinShader(rhi::ShaderType::eFragment, *fragmentShader)
            .setDepthStencil({
                .depthTest      = true,
                .depthWrite     = false,
                .depthCompareOp = rhi::CompareOp::eLessOrEqual,
            })
            .setRasterizer({
                .polygonMode = rhi::PolygonMode::eFill,
                .cullMode    = rhi::CullMode::eFront,
            })
            .setBlending(0, {.enabled = false})
            .build(getRenderDevice());
    }
} // namespace vultra
