#include "vultra/function/rendering/srp/builtin/passes/skybox_pass.hpp"

#include "vultra/core/rhi/util.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/rendering/framework/prepared_render_data.hpp"

#include <fg/FrameGraph.hpp>
#include <format>

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
            [cameraBlock, depth, environmentMap, useCubemapOverride = cubemapOverride != nullptr, &color](
                FrameGraph::Builder& builder,
                auto&) {
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
                if (!useCubemapOverride)
                {
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
                }
                color = builder.write(color,
                                      framegraph::Attachment {
                                          .index       = 0,
                                          .imageAspect = rhi::ImageAspect::eColor,
                                      });
            },
            [this, cubemapOverride](const auto&, FrameGraphPassResources&, void* ctxPtr) {
                VULTRA_SCOPED_FRAMEGRAPH_EXEC_CONTEXT(rc, ctxPtr);
                setRenderDevice(rc.rd);
                // The skybox samples a cubemap (samplerCube at set 3, binding 0). On WebGPU the environment
                // cubemap is produced by the cubemap_convert compute pass, which isn't implemented on this
                // backend yet, so the only available source is a 2D equirect whose view dimension mismatches
                // the shader's Cube binding (a fatal wgpu validation error). Skip until WebGPU cubemap
                // generation lands; deferred lighting already wrote the background color underneath.
                if (rc.rd.getBackendApi() == rhi::RenderBackendApi::eWebGPU)
                    return;
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
                rc.cb.beginRendering(framebufferInfo);
                {
                    const auto scopeName = std::format("{} {}x{}",
                                                       PASS_NAME,
                                                       framebufferInfo.area.extent.width,
                                                       framebufferInfo.area.extent.height);
                    RHI_GPU_ZONE(rc.cb, scopeName.c_str());
                    rc.cb.drawFullScreenTriangle();
                }
                rc.cb.endRendering();
            });

        return color;
    }

    rhi::GraphicsPipeline SkyboxPass::createPipeline(const rhi::PixelFormat depthFormat,
                                                     const rhi::PixelFormat colorFormat,
                                                     const uint32_t         viewMask) const
    {
        auto vertexShader = loadHighendShader("skybox", vshadersystem::ShaderStage::eVert);
        auto fragmentShader = loadHighendShader("skybox", vshadersystem::ShaderStage::eFrag);
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
