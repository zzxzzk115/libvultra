#include "vultra/function/rendering/srp/builtin/passes/test_pass.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/pixel_format.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"

#include <fg/FrameGraph.hpp>

namespace vultra
{
    constexpr auto PASS_NAME = "TestPass";

    FrameGraphResource TestPass::addPass(FrameGraphBuildContext& ctx)
    {
        const auto resolution = ctx.view.extent;

        struct PassData
        {
            FrameGraphResource depth;
            FrameGraphResource color;
        };
        auto data = ctx.fg.addCallbackPass<PassData>(
            PASS_NAME,
            [resolution](FrameGraph::Builder& builder, PassData& data) {
                PASS_SETUP_ZONE;

                data.color = builder.create<framegraph::FrameGraphTexture>(
                    "Test Pass Color",
                    {
                        .extent     = resolution,
                        .format     = rhi::PixelFormat::eRGBA8_UNorm,
                        .usageFlags = rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled,
                    });
                data.color = builder.write(data.color,
                                           framegraph::Attachment {
                                               .index       = 0,
                                               .imageAspect = rhi::ImageAspect::eColor,
                                               .clearValue  = framegraph::ClearValue::eOpaqueBlack,
                                           });

                data.depth = builder.create<framegraph::FrameGraphTexture>(
                    "Test Pass Depth",
                    {
                        .extent = resolution,
                        .format = rhi::PixelFormat::eDepth32F,
                        .usageFlags =
                            rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled | rhi::ImageUsage::eTransferSrc,
                    });
                data.depth = builder.write(data.depth,
                                           framegraph::Attachment {
                                               .imageAspect = rhi::ImageAspect::eDepth,
                                               .clearValue  = framegraph::ClearValue::eOne,
                                           });
            },
            [this](const auto&, FrameGraphPassResources&, void* ctx) {
                auto& rc = *static_cast<FrameGraphExecContext*>(ctx);
                setRenderDevice(rc.rd);
                if (!rc.ext.builtinShaderLib)
                    return;
                setShaderLib(*rc.ext.builtinShaderLib);

                RHI_GPU_ZONE(rc.cb, PASS_NAME);

                const auto* renderWorld = rc.view.renderWorld;
                const auto* gpuScene    = rc.view.gpuScene;
                auto*       cameraUbo   = rc.view.cameraUniformBuffer;

                if (!renderWorld || !gpuScene || !cameraUbo || !rc.framebufferInfo)
                    return;

                const auto* pipeline = getPipeline(rhi::getColorFormat(*rc.framebufferInfo, 0));
                if (!pipeline)
                    return;

                rc.cb.beginRendering(*rc.framebufferInfo).bindPipeline(*pipeline);

                rc.resourceSet[0] = {
                    {0, rhi::bindings::UniformBuffer {.buffer = cameraUbo}},
                    {1, rhi::bindings::StorageBuffer {.buffer = gpuScene->drawBuffer.get()}},
                    {2, rhi::bindings::StorageBuffer {.buffer = gpuScene->resources->materialTableBuffer.get()}},
                    {3, rhi::bindings::StorageBuffer {.buffer = gpuScene->resources->materialParams.gpu.get()}},
                };
                rc.resourceSet[3] = {
                    {4,
                     rhi::bindings::CombinedImageSamplerArray {
                         .textures    = gpuScene->resources->getBindlessTextureHandles(),
                         .imageAspect = rhi::ImageAspect::eColor,
                     }},
                };

                rc.bindDescriptorSets(*pipeline);

                rc.cb
                    .drawIndirect(rhi::DrawIndirectInfo {
                        .buffer       = &gpuScene->indirectBuffer.value(),
                        .firstCommand = 0,
                        .commandCount = static_cast<uint32_t>(gpuScene->indirectCommands.size()),
                        .gi =
                            rhi::GeometryInfo {
                                .indexBuffer = &gpuScene->resources->geometry.index32,
                                .numIndices  = gpuScene->resources->geometry.indexCountUsed,
                            },
                    })
                    .endRendering();

                rc.clear();
            });

        return data.color;
    }

    rhi::GraphicsPipeline TestPass::createPipeline(const rhi::PixelFormat colorFormat) const
    {
        auto vertexShaderVariantHash = getShaderLib().computeVariantHash("mesh.vert",
                                                                         vshadersystem::ShaderStage::eVert,
                                                                         {
                                                                             {"VTX_HAS_NORMAL", 1},
                                                                             {"VTX_HAS_COLOR", 0},
                                                                             {"VTX_HAS_UV0", 1},
                                                                             {"VTX_HAS_UV1", 0},
                                                                             {"VTX_HAS_TANGENT", 1},
                                                                         });
        auto vertexShader            = getShaderLib().load(vertexShaderVariantHash, vshadersystem::ShaderStage::eVert);
        if (!vertexShader)
        {
            VULTRA_CORE_ERROR("[TestPass] Failed to load vertex shader variant");
            return {};
        }

        auto fragmentShaderVariantHash =
            getShaderLib().computeVariantHash("base.frag", vshadersystem::ShaderStage::eFrag, {{"VTX_HAS_UV0", 1}});
        auto fragmentShader = getShaderLib().load(fragmentShaderVariantHash, vshadersystem::ShaderStage::eFrag);
        if (!fragmentShader)
        {
            VULTRA_CORE_ERROR("[TestPass] Failed to load fragment shader variant");
            return {};
        }

        return rhi::GraphicsPipeline::Builder {}
            .setColorFormats({colorFormat})
            .setDepthFormat(rhi::PixelFormat::eDepth32F)
            .addBuiltinShader(rhi::ShaderType::eVertex, vertexShader->spirv)
            .addBuiltinShader(rhi::ShaderType::eFragment, fragmentShader->spirv)
            .setDepthStencil({
                .depthTest  = true,
                .depthWrite = true,
            })
            .setBlending(0, {.enabled = false})
            .build(getRenderDevice());
    }
} // namespace vultra
