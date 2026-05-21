#include "vultra/function/rendering/srp/builtin/passes/fxaa_pass.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/structs/pixel_format.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"

#include <fg/FrameGraph.hpp>

#include <glm/ext/vector_float2.hpp>

namespace vultra
{
    FxaaPass::FxaaPass() { setShaderProfile(rhi::ShaderProfile::eGeneral); }

    namespace
    {
        constexpr auto PASS_NAME = "FXAAPass";

        struct FxaaPushConstants
        {
            glm::vec2 resolution {};
        };
    } // namespace

    FrameGraphResource FxaaPass::addPass(FrameGraphBuildContext& ctx, FrameGraphResource source)
    {
        const auto extent = ctx.fg.getDescriptor<framegraph::FrameGraphTexture>(source).extent;

        struct PassData
        {
            FrameGraphResource source;
            FrameGraphResource output;
        };

        const auto data = ctx.fg.addCallbackPass<PassData>(
            PASS_NAME,
            [source, extent](FrameGraph::Builder& builder, PassData& pd) {
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
                    "FXAAOutput",
                    {
                        .extent     = extent,
                        .format     = rhi::PixelFormat::eRGBA8_UNorm,
                        .usageFlags = rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled,
                    });
                pd.output = builder.write(pd.output,
                                          framegraph::Attachment {
                                              .index       = 0,
                                              .imageAspect = rhi::ImageAspect::eColor,
                                              .clearValue  = framegraph::ClearValue::eOpaqueBlack,
                                          });
            },
            [this, extent](const PassData&, FrameGraphPassResources&, void* ctxPtr) {
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

                FxaaPushConstants pc {
                    .resolution = glm::vec2(static_cast<float>(extent.width), static_cast<float>(extent.height)),
                };

                rc.overrideSampler(rc.resourceSet[3][0], rc.ext.samplers["bilinear"]);
                rc.cb.bindPipeline(*pipeline);
                rc.bindDescriptorSets(*pipeline);
                rc.cb.pushConstants(rhi::ShaderStages::eFragment, 0, &pc);
                rc.cb.beginRendering(rc.framebufferInfo().value()).drawFullScreenTriangle().endRendering();
            });

        return data.output;
    }

    rhi::GraphicsPipeline FxaaPass::createPipeline(const rhi::PixelFormat colorFormat) const
    {
        auto vertexShader = loadGeneralShader("fullscreen_triangle.vert", vshadersystem::ShaderStage::eVert);
        if (!vertexShader)
        {
            VULTRA_CORE_ERROR("[FXAAPass] Failed to load vertex shader");
            return {};
        }

        auto fragmentShader = loadGeneralShader("fxaa.frag", vshadersystem::ShaderStage::eFrag);
        if (!fragmentShader)
        {
            VULTRA_CORE_ERROR("[FXAAPass] Failed to load fragment shader");
            return {};
        }

        return rhi::GraphicsPipeline::Builder {}
            .setColorFormats({colorFormat})
            .setInputAssembly({})
            .addBuiltinShader(rhi::ShaderType::eVertex, vertexShader->spirv)
            .addBuiltinShader(rhi::ShaderType::eFragment, fragmentShader->spirv)
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
