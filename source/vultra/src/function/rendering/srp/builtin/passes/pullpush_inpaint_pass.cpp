#include "vultra/function/rendering/srp/builtin/passes/pullpush_inpaint_pass.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/structs/pixel_format.hpp"
#include "vultra/core/rhi/texture.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"
#include "vultra/function/rendering/srp/render_target_desc.hpp"

#include <fg/FrameGraph.hpp>

#include <algorithm>
#include <string>
#include <vector>

namespace vultra
{
    namespace
    {
        struct PullPushConstants
        {
            int32_t lod {0};
            float   depthThreshold {0.0f};
        };

        [[nodiscard]] std::vector<rhi::Extent2D> makeMipSizes(const rhi::Extent2D extent)
        {
            std::vector<rhi::Extent2D> sizes;
            const uint32_t             mipLevelCount = rhi::calcMipLevels(extent);
            sizes.reserve(mipLevelCount);
            for (uint32_t level = 0u; level < mipLevelCount; ++level)
            {
                const auto size = rhi::calcMipSize(glm::uvec3 {extent.width, extent.height, 1u}, level);
                sizes.push_back({
                    .width  = size.x,
                    .height = size.y,
                });
            }
            return sizes;
        }

        [[nodiscard]] std::string
        makeMipPassName(std::string_view prefix, const uint32_t lod, const rhi::Extent2D extent)
        {
            return std::string(prefix) + "[L" + std::to_string(lod) + " " + std::to_string(extent.width) + "x" +
                   std::to_string(extent.height) + "]";
        }
    } // namespace

    PullPyramidPass::PullPyramidPass() { setShaderProfile(rhi::ShaderProfile::eGeneral); }

    PullPushMipData PullPyramidPass::addPass(FrameGraphBuildContext&  ctx,
                                             const FrameGraphResource pyramid,
                                             const uint32_t           lod,
                                             const rhi::Extent2D      dstExtent,
                                             const bool               useDepthAware,
                                             const float              depthThreshold)
    {
        const auto pyramidDesc = ctx.fg.getDescriptor<framegraph::FrameGraphTexture>(pyramid);

        struct PassData
        {
            FrameGraphResource pyramid;
            FrameGraphResource output;
        };

        const auto passName = makeMipPassName("ViewSynthesisPull", lod, dstExtent);
        const auto data     = ctx.fg.addCallbackPass<PassData>(
            passName,
            [pyramid,
             outputDesc = makeInheritedTextureDesc(pyramidDesc, rhi::PixelFormat::eRGBA16F),
             lod,
             dstExtent,
             outputName = passName + " Output"](FrameGraph::Builder& builder, PassData& pd) {
                PASS_SETUP_ZONE;

                builder.read(pyramid,
                             framegraph::TextureRead {
                                     .binding =
                                         {
                                             .location      = {.set = 3, .binding = 0},
                                             .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                     },
                                     .type        = framegraph::TextureRead::Type::eCombinedImageSampler,
                                     .imageAspect = rhi::ImageAspect::eColor,
                             });
                builder.read(pyramid,
                             framegraph::TextureRead {
                                     .binding =
                                         {
                                             .location      = {.set = 3, .binding = 1},
                                             .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                     },
                                     .type        = framegraph::TextureRead::Type::eCombinedImageSampler,
                                     .imageAspect = rhi::ImageAspect::eColor,
                             });

                auto desc         = outputDesc;
                desc.extent       = dstExtent;
                desc.numMipLevels = 1u;
                pd.output         = builder.create<framegraph::FrameGraphTexture>(outputName, desc);
                pd.output         = builder.write(pd.output,
                                          framegraph::Attachment {
                                                          .index       = 0,
                                                          .imageAspect = rhi::ImageAspect::eColor,
                                                          .clearValue  = framegraph::ClearValue::eTransparentBlack,
                                          });
                pd.pyramid        = builder.write(pyramid);
            },
            [this, lod, passName, useDepthAware, depthThreshold](
                const PassData& data, FrameGraphPassResources& resources, void* ctxPtr) {
                VULTRA_SCOPED_FRAMEGRAPH_EXEC_CONTEXT(rc, ctxPtr);
                setRenderDevice(rc.rd);
                if (!rc.ext.builtinShaderLib)
                    return;
                setShaderLib(*rc.ext.builtinShaderLib);

                RHI_GPU_ZONE(rc.cb, passName.c_str());

                assert(rc.framebufferInfo().has_value());
                const auto  framebufferInfo = rc.framebufferInfo().value();
                const auto* pipeline =
                    getPipeline(rhi::getColorFormat(framebufferInfo, 0), framebufferInfo.viewMask, useDepthAware);
                if (!pipeline)
                    return;

                PullPushConstants pc {.lod = static_cast<int32_t>(lod), .depthThreshold = depthThreshold};

                auto& pyramidTexture = *resources.get<framegraph::FrameGraphTexture>(data.pyramid).texture;
                rhi::prepareForReading(rc.cb, pyramidTexture, lod);
                rhi::prepareForReading(rc.cb, pyramidTexture, lod + 1u);
                rc.overrideSampler(rc.resourceSet[3][0], rc.ext.samplers["bilinear"]);
                rc.overrideSampler(rc.resourceSet[3][1], rc.ext.samplers["nearest"]);
                rc.cb.bindPipeline(*pipeline);
                rc.bindDescriptorSets(*pipeline);
                rc.cb.pushConstants(rhi::ShaderStages::eFragment, 0, &pc);
                rc.cb.beginRendering(framebufferInfo).drawFullScreenTriangle().endRendering();

                auto& outputTexture = *resources.get<framegraph::FrameGraphTexture>(data.output).texture;
                rc.cb.blit(outputTexture, pyramidTexture, rhi::TexelFilter::eNearest, 0u, lod);
                rhi::prepareForReading(rc.cb, outputTexture);
                rhi::prepareForReading(rc.cb, pyramidTexture, lod);
            });

        return {.pyramid = data.pyramid, .output = data.output};
    }

    rhi::GraphicsPipeline PullPyramidPass::createPipeline(const rhi::PixelFormat colorFormat,
                                                          const uint32_t         viewMask,
                                                          const bool             useDepthAware) const
    {
        auto vertexShader = loadGeneralShader("fullscreen_triangle.vert", vshadersystem::ShaderStage::eVert);
        if (!vertexShader)
        {
            VULTRA_CORE_ERROR("[PullPyramidPass] Failed to load vertex shader");
            return {};
        }

        rhi::ShaderLibraryRuntime::KeywordValues fragmentKeywords {
            {"USE_MULTIVIEW", viewMask != 0u ? 1u : 0u},
            {"USE_DEPTH_AWARE", useDepthAware ? 1u : 0u},
        };
        auto fragmentShader = loadGeneralShader("pullpush_pull.frag", vshadersystem::ShaderStage::eFrag, fragmentKeywords);
        if (!fragmentShader)
        {
            VULTRA_CORE_ERROR("[PullPyramidPass] Failed to load fragment shader");
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

    PushPyramidPass::PushPyramidPass() { setShaderProfile(rhi::ShaderProfile::eGeneral); }

    PullPushMipData PushPyramidPass::addPass(FrameGraphBuildContext&  ctx,
                                             const FrameGraphResource pyramid,
                                             const uint32_t           lod,
                                             const rhi::Extent2D      dstExtent,
                                             const bool               useDepthAware,
                                             const float              depthThreshold)
    {
        const auto pyramidDesc = ctx.fg.getDescriptor<framegraph::FrameGraphTexture>(pyramid);

        struct PassData
        {
            FrameGraphResource pyramid;
            FrameGraphResource output;
        };

        const auto passName = makeMipPassName("ViewSynthesisPush", lod + 1u, dstExtent);
        const auto data     = ctx.fg.addCallbackPass<PassData>(
            passName,
            [pyramid,
             outputDesc = makeInheritedTextureDesc(pyramidDesc, rhi::PixelFormat::eRGBA16F),
             dstExtent,
             outputName = passName + " Output"](FrameGraph::Builder& builder, PassData& pd) {
                PASS_SETUP_ZONE;

                builder.read(pyramid,
                             framegraph::TextureRead {
                                     .binding =
                                         {
                                             .location      = {.set = 3, .binding = 0},
                                             .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                     },
                                     .type        = framegraph::TextureRead::Type::eCombinedImageSampler,
                                     .imageAspect = rhi::ImageAspect::eColor,
                             });

                auto desc         = outputDesc;
                desc.extent       = dstExtent;
                desc.numMipLevels = 1u;
                pd.output         = builder.create<framegraph::FrameGraphTexture>(outputName, desc);
                pd.output         = builder.write(pd.output,
                                          framegraph::Attachment {
                                                          .index       = 0,
                                                          .imageAspect = rhi::ImageAspect::eColor,
                                                          .clearValue  = framegraph::ClearValue::eTransparentBlack,
                                          });
                pd.pyramid        = builder.write(pyramid);
            },
            [this, lod, passName, useDepthAware, depthThreshold](
                const PassData& data, FrameGraphPassResources& resources, void* ctxPtr) {
                VULTRA_SCOPED_FRAMEGRAPH_EXEC_CONTEXT(rc, ctxPtr);
                setRenderDevice(rc.rd);
                if (!rc.ext.builtinShaderLib)
                    return;
                setShaderLib(*rc.ext.builtinShaderLib);

                RHI_GPU_ZONE(rc.cb, passName.c_str());

                assert(rc.framebufferInfo().has_value());
                const auto  framebufferInfo = rc.framebufferInfo().value();
                const auto* pipeline =
                    getPipeline(rhi::getColorFormat(framebufferInfo, 0), framebufferInfo.viewMask, useDepthAware);
                if (!pipeline)
                    return;

                PullPushConstants pc {.lod = static_cast<int32_t>(lod), .depthThreshold = depthThreshold};

                auto& pyramidTexture = *resources.get<framegraph::FrameGraphTexture>(data.pyramid).texture;
                rhi::prepareForReading(rc.cb, pyramidTexture, lod);
                rc.overrideSampler(rc.resourceSet[3][0], rc.ext.samplers["nearest"]);
                rc.cb.bindPipeline(*pipeline);
                rc.bindDescriptorSets(*pipeline);
                rc.cb.pushConstants(rhi::ShaderStages::eFragment, 0, &pc);
                rc.cb.beginRendering(framebufferInfo).drawFullScreenTriangle().endRendering();

                auto& outputTexture = *resources.get<framegraph::FrameGraphTexture>(data.output).texture;
                rc.cb.blit(outputTexture, pyramidTexture, rhi::TexelFilter::eNearest, 0u, lod + 1u);
                rhi::prepareForReading(rc.cb, outputTexture);
                rhi::prepareForReading(rc.cb, pyramidTexture, lod + 1u);
            });

        return {.pyramid = data.pyramid, .output = data.output};
    }

    rhi::GraphicsPipeline PushPyramidPass::createPipeline(const rhi::PixelFormat colorFormat,
                                                          const uint32_t         viewMask,
                                                          const bool             useDepthAware) const
    {
        auto vertexShader = loadGeneralShader("fullscreen_triangle.vert", vshadersystem::ShaderStage::eVert);
        if (!vertexShader)
        {
            VULTRA_CORE_ERROR("[PushPyramidPass] Failed to load vertex shader");
            return {};
        }

        rhi::ShaderLibraryRuntime::KeywordValues fragmentKeywords {
            {"USE_MULTIVIEW", viewMask != 0u ? 1u : 0u},
            {"USE_DEPTH_AWARE", useDepthAware ? 1u : 0u},
        };
        auto fragmentShader = loadGeneralShader("pullpush_push.frag", vshadersystem::ShaderStage::eFrag, fragmentKeywords);
        if (!fragmentShader)
        {
            VULTRA_CORE_ERROR("[PushPyramidPass] Failed to load fragment shader");
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

    FrameGraphResource PullPushInpaintPass::addPass(FrameGraphBuildContext&      ctx,
                                                    const FrameGraphResource     warped,
                                                    const ViewSynthesisSettings& settings)
    {
        const auto useDepthAware  = settings.useDepthAware;
        const auto depthThreshold = std::max(settings.depthThreshold, 0.0f);
        const auto warpedDesc     = ctx.fg.getDescriptor<framegraph::FrameGraphTexture>(warped);
        const auto sizes          = makeMipSizes(warpedDesc.extent);
        const auto mipLevels      = static_cast<uint32_t>(sizes.size());
        auto       pyramidDesc    = makeInheritedTextureDesc(warpedDesc, rhi::PixelFormat::eRGBA16F);
        pyramidDesc.numMipLevels  = mipLevels;
        pyramidDesc.usageFlags    = rhi::ImageUsage::eTransferDst | rhi::ImageUsage::eTransferSrc |
                                 rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled;

        struct InitData
        {
            FrameGraphResource input;
            FrameGraphResource pyramid;
        };

        const auto initData = ctx.fg.addCallbackPass<InitData>(
            "ViewSynthesisPullPushInit",
            [warped, pyramidDesc](FrameGraph::Builder& builder, InitData& pd) {
                PASS_SETUP_ZONE;

                pd.input = builder.read(warped,
                                        framegraph::TextureRead {
                                            .binding =
                                                {
                                                    .location      = {.set = 0, .binding = 0},
                                                    .pipelineStage = framegraph::PipelineStage::eTransfer,
                                                },
                                            .type        = framegraph::TextureRead::Type::eSampledImage,
                                            .imageAspect = rhi::ImageAspect::eColor,
                                        });
                pd.pyramid = builder.create<framegraph::FrameGraphTexture>("ViewSynthesisPullPushPyramid", pyramidDesc);
                pd.pyramid = builder.write(pd.pyramid);
            },
            [](const InitData& data, FrameGraphPassResources& resources, void* ctxPtr) {
                VULTRA_SCOPED_FRAMEGRAPH_EXEC_CONTEXT(rc, ctxPtr);
                RHI_GPU_ZONE(rc.cb, "ViewSynthesisPullPushInit");

                auto& inputTexture   = *resources.get<framegraph::FrameGraphTexture>(data.input).texture;
                auto& pyramidTexture = *resources.get<framegraph::FrameGraphTexture>(data.pyramid).texture;
                rc.cb.blit(inputTexture, pyramidTexture, rhi::TexelFilter::eNearest, 0u, 0u);
                rhi::prepareForReading(rc.cb, inputTexture);
                rhi::prepareForReading(rc.cb, pyramidTexture, 0u);
            });

        FrameGraphResource pyramid = initData.pyramid;
        for (uint32_t lod = 0u; lod + 1u < mipLevels; ++lod)
            pyramid = m_PushPass.addPass(ctx, pyramid, lod, sizes[lod + 1u], useDepthAware, depthThreshold).pyramid;

        FrameGraphResource repaired = pyramid;
        for (uint32_t lod = mipLevels - 1u; lod > 0u; --lod)
        {
            const auto pullData =
                m_PullPass.addPass(ctx, repaired, lod - 1u, sizes[lod - 1u], useDepthAware, depthThreshold);
            repaired = pullData.pyramid;
            if (lod == 1u)
                return pullData.output;
        }

        return warped;
    }
} // namespace vultra
