#include "vultra/function/rendering/srp/builtin/passes/final_composition_pass.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/rendering/srp/builtin/resource_keys.hpp"

#include <fg/FrameGraph.hpp>

namespace vultra
{
    constexpr auto PASS_NAME = "FinalCompositionPass";

    FrameGraphResource FinalCompositionPass::compose(FrameGraphBuildContext& ctx, FrameGraphResource target)
    {
        auto source = ctx.data.get(kResKey_FinalCompositionSource);

        ctx.fg.addCallbackPass(
            PASS_NAME,
            [source, &target](FrameGraph::Builder& builder, auto&) {
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

                target = builder.write(target,
                                       framegraph::Attachment {
                                           .index       = 0,
                                           .imageAspect = rhi::ImageAspect::eColor,
                                           .clearValue  = framegraph::ClearValue::eOpaqueBlack,
                                       });
            },
            [this, target](const auto&, FrameGraphPassResources&, void* ctx) {
                auto& rc = *static_cast<FrameGraphExecContext*>(ctx);
                setRenderDevice(rc.rd);
                if (!rc.ext.builtinShaderLib)
                    return;
                setShaderLib(*rc.ext.builtinShaderLib);

                RHI_GPU_ZONE(rc.cb, PASS_NAME);

                assert(rc.framebufferInfo().has_value());
                const auto* pipeline = getPipeline(rhi::getColorFormat(rc.framebufferInfo().value(), 0));
                if (pipeline)
                {
                    rc.overrideSampler(rc.resourceSet[3][0], rc.ext.samplers["nearest"]);
                    rc.cb.bindPipeline(*pipeline);
                    rc.bindDescriptorSets(*pipeline);
                    rc.cb.beginRendering(rc.framebufferInfo().value()).drawFullScreenTriangle().endRendering();
                    rc.clear();
                }
            });

        return target;
    }

    rhi::GraphicsPipeline FinalCompositionPass::createPipeline(const rhi::PixelFormat colorFormat) const
    {
        auto vertexShaderVariantHash =
            getShaderLib().computeVariantHash("fullscreen_triangle.vert", vshadersystem::ShaderStage::eVert, {});
        auto vertexShader = getShaderLib().load(vertexShaderVariantHash, vshadersystem::ShaderStage::eVert);
        if (!vertexShader)
        {
            VULTRA_CORE_ERROR("[FinalCompositionPass] Failed to load vertex shader variant");
            return {};
        }

        auto fragmentShaderVariantHash =
            getShaderLib().computeVariantHash("final_composition.frag", vshadersystem::ShaderStage::eFrag, {});
        auto fragmentShader = getShaderLib().load(fragmentShaderVariantHash, vshadersystem::ShaderStage::eFrag);
        if (!fragmentShader)
        {
            VULTRA_CORE_ERROR("[FinalCompositionPass] Failed to load fragment shader variant");
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
                .cullMode    = rhi::CullMode::eFront,
            })
            .setBlending(0, {.enabled = false})
            .build(getRenderDevice());
    }
} // namespace vultra
