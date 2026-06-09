#include "vultra/function/rendering/srp/builtin/passes/xr_view_synthesis_pass.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/structs/geometry_info.hpp"
#include "vultra/core/rhi/structs/pixel_format.hpp"
#include "vultra/core/rhi/texture.hpp"
#include "vultra/function/framegraph/framegraph_buffer.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"
#include "vultra/function/rendering/framework/resource_uploader.hpp"
#include "vultra/function/rendering/render_structs.hpp"
#include "vultra/function/rendering/srp/render_target_desc.hpp"
#include "vultra/function/rendering/srp/render_view.hpp"

#include <fg/FrameGraph.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/mat4x4.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <vector>

namespace vultra
{
    namespace
    {
        constexpr auto GEOMETRY_WARP_PASS_NAME = "XRGeometryWarp";

        constexpr std::array<XrViewSynthesisPass::BackendInfo, 2> kWarpingBackends {{
            {"geometry",
             "Geometry",
             "Geometry-shader warping with disocclusion-hole detection (desktop/Vulkan profile)."},
            {"none", "None", "Forward source color without warping."},
        }};

        constexpr std::array<XrViewSynthesisPass::BackendInfo, 2> kInpaintingBackends {{
            {"pull_push", "Pull Push", "Depth-aware pull-push repair using alpha validity."},
            {"none", "None", "Keep warped holes visible."},
        }};

        constexpr std::array<XrViewSynthesisPass::ViewInfo, 3> kSourceViews {{
            {"left", "Left"},
            {"right", "Right"},
            {"primary", "Primary"},
        }};

        constexpr std::array<XrViewSynthesisPass::ViewInfo, 4> kTargetViews {{
            {"right", "Right"},
            {"left", "Left"},
            {"primary", "Primary"},
            {"stereo", "Stereo"},
        }};

        enum class XrSynthesisView : uint32_t
        {
            ePrimary = 0,
            eLeft    = 1,
            eRight   = 2,
            eStereo  = 3,
        };

        struct XrGeometryWarpPushConstants
        {
            glm::vec2 resolution {};
            uint32_t  sourceView {static_cast<uint32_t>(XrSynthesisView::eLeft)};
            uint32_t  targetView {static_cast<uint32_t>(XrSynthesisView::eRight)};
            uint32_t  gridSize {4};
            float     sideLenThreshold {0.05f};
            uint32_t  useDepthAware {1u};
        };

        // Matches XrWarpBlock in xr_view_synthesis_geometry_warp.vert (std140 UBO).
        struct XrWarpBlock
        {
            glm::mat4 sourceInvViewProj {1.0f};
            glm::mat4 targetViewProj[2] {glm::mat4 {1.0f}, glm::mat4 {1.0f}};
        };

        struct XrPullPushConstants
        {
            int32_t lod {0};
            float   depthThreshold {0.0f};
        };

        [[nodiscard]] uint32_t divRoundUp(const uint32_t x, const uint32_t y) { return y == 0u ? x : (x + y - 1u) / y; }

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

        [[nodiscard]] std::string normalizeName(std::string_view text)
        {
            std::string out;
            out.reserve(text.size());
            for (const char ch : text)
                out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
            return out;
        }

        [[nodiscard]] bool containsBackend(std::span<const XrViewSynthesisPass::BackendInfo> backends,
                                           std::string_view                                  name)
        {
            return std::ranges::any_of(backends, [name](const auto& backend) { return backend.value == name; });
        }

        void warnUnknownBackendOnce(std::string_view category, std::string_view requested, std::string_view fallback)
        {
            static std::array<std::string, 8> warned {};
            static size_t                     warnedCount = 0;

            const std::string key = std::string(category) + ":" + std::string(requested);
            if (std::ranges::find(warned, key) != warned.end())
                return;

            if (warnedCount < warned.size())
                warned[warnedCount++] = key;

            VULTRA_CORE_WARN("[XrViewSynthesis] Unknown {} backend '{}'; using '{}'.", category, requested, fallback);
        }

        [[nodiscard]] std::string_view resolveWarpingBackend(std::string_view requested)
        {
            if (containsBackend(kWarpingBackends, requested))
                return requested;
            warnUnknownBackendOnce("warping", requested, "geometry");
            return "geometry";
        }

        [[nodiscard]] std::string_view resolveInpaintingBackend(std::string_view requested)
        {
            if (containsBackend(kInpaintingBackends, requested))
                return requested;
            warnUnknownBackendOnce("inpainting", requested, "pull_push");
            return "pull_push";
        }
    } // namespace

    XrGeometryWarpPass::XrGeometryWarpPass() { setShaderProfile(rhi::ShaderProfile::eGeneral); }

    std::string_view XrGeometryWarpPass::name() const { return "geometry"; }

    FrameGraphResource XrGeometryWarpPass::addPass(FrameGraphBuildContext&        ctx,
                                                   const FrameGraphResource       source,
                                                   const FrameGraphResource       depth,
                                                   const XrViewSynthesisSettings& settings)
    {
        const auto sourceDesc  = ctx.fg.getDescriptor<framegraph::FrameGraphTexture>(source);
        const auto depthDesc   = ctx.fg.getDescriptor<framegraph::FrameGraphTexture>(depth);
        const auto gridSize    = std::max(settings.gridSize, 1u);
        const auto cellsX      = std::max(1u, divRoundUp(sourceDesc.extent.width, gridSize));
        const auto cellsY      = std::max(1u, divRoundUp(sourceDesc.extent.height, gridSize));
        const auto vertexCount = cellsX * cellsY * 6u;
        const auto sourceView  = normalizeName(settings.sourceView);
        const auto targetView  = normalizeName(settings.targetView);
        const auto sourceViewId = sourceView == "right" ? 2u : sourceView == "primary" ? 0u : 1u;
        const auto targetViewId = targetView == "left"    ? 1u :
                                  targetView == "primary" ? 0u :
                                  targetView == "stereo"  ? 3u :
                                                             2u;
        const auto sideLenThreshold = std::max(settings.sideLenThreshold, 0.0f);
        const auto useDepthAware    = settings.useDepthAware ? 1u : 0u;

        // Build the source->target reprojection block from the per-eye cameras.
        // Stereo uses both eye cameras; without them (mono/preview) the warp is an
        // identity pass-through since there is no second viewpoint to synthesize.
        const auto&    view     = ctx.view();
        const uint32_t srcLayer = sourceViewId == 2u ? 1u : 0u;
        const bool     haveStereoCameras =
            view.multiviewCameraCount >= 2u && view.multiviewCameras[0] && view.multiviewCameras[1];

        XrWarpBlock         warpBlock {};
        const RenderCamera* srcCam = haveStereoCameras ? view.multiviewCameras[srcLayer] : view.camera;
        if (srcCam)
            warpBlock.sourceInvViewProj = srcCam->inverseViewProjection;
        for (uint32_t i = 0; i < 2u; ++i)
        {
            const RenderCamera* tgt = haveStereoCameras ? view.multiviewCameras[i] : srcCam;
            if (tgt)
                warpBlock.targetViewProj[i] = tgt->viewProjection;
        }
        if (!haveStereoCameras)
        {
            static bool warnedMono = false;
            if (!warnedMono)
            {
                warnedMono = true;
                VULTRA_CORE_WARN(
                    "[XrGeometryWarp] No stereo cameras available; warp passes source through (mono/preview).");
            }
        }

        const auto warpBlockResource = uploadFrameGraphStruct(ctx.fg,
                                                              ctx.frameResources,
                                                              ctx.rd,
                                                              "UploadXrWarpBlock",
                                                              "XrWarpBlock",
                                                              framegraph::BufferType::eUniformBuffer,
                                                              warpBlock);

        struct PassData
        {
            FrameGraphResource warpBlock;
            FrameGraphResource source;
            FrameGraphResource depth;
            FrameGraphResource warped;
            FrameGraphResource warpedDepth;
        };

        const auto data = ctx.fg.addCallbackPass<PassData>(
            GEOMETRY_WARP_PASS_NAME,
            [source, depth, sourceDesc, depthDesc, warpBlockResource](FrameGraph::Builder& builder, PassData& pd) {
                PASS_SETUP_ZONE;

                pd.warpBlock = builder.read(warpBlockResource,
                                            framegraph::BindingInfo {
                                                .location      = {.set = 1, .binding = 0},
                                                .pipelineStage = framegraph::PipelineStage::eVertexShader,
                                            });
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
                pd.depth  = builder.read(depth,
                                        framegraph::TextureRead {
                                             .binding =
                                                 {
                                                     .location      = {.set = 3, .binding = 1},
                                                     .pipelineStage = framegraph::PipelineStage::eVertexShader,
                                                },
                                             .type        = framegraph::TextureRead::Type::eCombinedImageSampler,
                                             .imageAspect = depthDesc.format == rhi::PixelFormat::eDepth32F ?
                                                                rhi::ImageAspect::eDepth :
                                                                rhi::ImageAspect::eColor,
                                        });

                pd.warped = builder.create<framegraph::FrameGraphTexture>(
                    "XRGeometryWarpColor", makeInheritedTextureDesc(sourceDesc, rhi::PixelFormat::eRGBA16F));
                pd.warped = builder.write(pd.warped,
                                          framegraph::Attachment {
                                              .index       = 0,
                                              .imageAspect = rhi::ImageAspect::eColor,
                                              // Uncovered pixels stay alpha=1 (hole) for the pull-push stage.
                                              .clearValue  = framegraph::ClearValue::eOpaqueBlack,
                                          });

                auto depthOutputDesc       = makeInheritedTextureDesc(sourceDesc, rhi::PixelFormat::eDepth32F);
                depthOutputDesc.usageFlags = rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled;
                pd.warpedDepth = builder.create<framegraph::FrameGraphTexture>("XRGeometryWarpDepth", depthOutputDesc);
                pd.warpedDepth = builder.write(pd.warpedDepth,
                                               framegraph::Attachment {
                                                   .imageAspect = rhi::ImageAspect::eDepth,
                                                   .clearValue  = framegraph::ClearValue::eOne,
                                               });
            },
            [this, sourceDesc, vertexCount, gridSize, sideLenThreshold, useDepthAware, sourceViewId, targetViewId](
                const PassData&, FrameGraphPassResources&, void* ctxPtr) {
                VULTRA_SCOPED_FRAMEGRAPH_EXEC_CONTEXT(rc, ctxPtr);
                setRenderDevice(rc.rd);
                if (!rc.ext.builtinShaderLib)
                    return;
                setShaderLib(*rc.ext.builtinShaderLib);

                RHI_GPU_ZONE(rc.cb, GEOMETRY_WARP_PASS_NAME);

                assert(rc.framebufferInfo().has_value());
                const auto  framebufferInfo = rc.framebufferInfo().value();
                const auto* pipeline = getPipeline(rhi::getColorFormat(framebufferInfo, 0), framebufferInfo.viewMask);
                if (!pipeline)
                    return;

                XrGeometryWarpPushConstants pc {
                    .resolution       = glm::vec2(static_cast<float>(sourceDesc.extent.width),
                                            static_cast<float>(sourceDesc.extent.height)),
                    .sourceView       = sourceViewId,
                    .targetView       = targetViewId,
                    .gridSize         = gridSize,
                    .sideLenThreshold = sideLenThreshold,
                    .useDepthAware    = useDepthAware,
                };

                rc.overrideSampler(rc.resourceSet[3][0], rc.ext.samplers["bilinear"]);
                rc.overrideSampler(rc.resourceSet[3][1], rc.ext.samplers["nearest"]);
                rc.cb.bindPipeline(*pipeline);
                rc.bindDescriptorSets(*pipeline);
                rc.cb.pushConstants(
                    rhi::ShaderStages::eVertex | rhi::ShaderStages::eGeometry | rhi::ShaderStages::eFragment, 0, &pc);
                rc.cb.beginRendering(framebufferInfo)
                    .draw({.topology = rhi::PrimitiveTopology::eTriangleList, .numVertices = vertexCount})
                    .endRendering();
            });

        return data.warped;
    }

    rhi::GraphicsPipeline XrGeometryWarpPass::createPipeline(const rhi::PixelFormat colorFormat,
                                                             const uint32_t         viewMask) const
    {
        rhi::ShaderLibraryRuntime::KeywordValues keywords {
            {"USE_MULTIVIEW", viewMask != 0u ? 1u : 0u},
        };
        auto vertexShader =
            loadGeneralShader("xr_view_synthesis_geometry_warp.vert", vshadersystem::ShaderStage::eVert, keywords);
        if (!vertexShader)
        {
            VULTRA_CORE_ERROR("[XrGeometryWarpPass] Failed to load vertex shader");
            return {};
        }

        auto geometryShader =
            loadGeneralShader("xr_view_synthesis_geometry_warp.geom", vshadersystem::ShaderStage::eGeom, keywords);
        if (!geometryShader)
        {
            // Geometry shaders are unavailable on the WebGPU/compatibility profile
            // (warn-skipped by the toolchain). Select a different warp backend there.
            VULTRA_CORE_ERROR("[XrGeometryWarpPass] Failed to load geometry shader (unsupported on this profile?)");
            return {};
        }

        auto fragmentShader =
            loadGeneralShader("xr_view_synthesis_geometry_warp.frag", vshadersystem::ShaderStage::eFrag, keywords);
        if (!fragmentShader)
        {
            VULTRA_CORE_ERROR("[XrGeometryWarpPass] Failed to load fragment shader");
            return {};
        }

        return rhi::GraphicsPipeline::Builder {}
            .setColorFormats({colorFormat})
            .setDepthFormat(rhi::PixelFormat::eDepth32F)
            .setViewMask(viewMask)
            .setInputAssembly({})
            .setTopology(rhi::PrimitiveTopology::eTriangleList)
            .addBuiltinShader(rhi::ShaderType::eVertex, *vertexShader)
            .addBuiltinShader(rhi::ShaderType::eGeometry, *geometryShader)
            .addBuiltinShader(rhi::ShaderType::eFragment, *fragmentShader)
            .setDepthStencil({
                .depthTest      = true,
                .depthWrite     = true,
                .depthCompareOp = rhi::CompareOp::eLess,
            })
            .setRasterizer({
                .polygonMode = rhi::PolygonMode::eFill,
                .cullMode    = rhi::CullMode::eNone,
            })
            .setBlending(0, {.enabled = false})
            .build(getRenderDevice());
    }

    XrPullPyramidPass::XrPullPyramidPass() { setShaderProfile(rhi::ShaderProfile::eGeneral); }

    XrPullPushMipData XrPullPyramidPass::addPass(FrameGraphBuildContext&  ctx,
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

        const auto passName = makeMipPassName("XRViewSynthesisPull", lod, dstExtent);
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

                XrPullPushConstants pc {.lod = static_cast<int32_t>(lod), .depthThreshold = depthThreshold};

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

    rhi::GraphicsPipeline XrPullPyramidPass::createPipeline(const rhi::PixelFormat colorFormat,
                                                            const uint32_t         viewMask,
                                                            const bool             useDepthAware) const
    {
        auto vertexShader = loadGeneralShader("fullscreen_triangle.vert", vshadersystem::ShaderStage::eVert);
        if (!vertexShader)
        {
            VULTRA_CORE_ERROR("[XrPullPyramidPass] Failed to load vertex shader");
            return {};
        }

        rhi::ShaderLibraryRuntime::KeywordValues fragmentKeywords {
            {"USE_MULTIVIEW", viewMask != 0u ? 1u : 0u},
            {"USE_DEPTH_AWARE", useDepthAware ? 1u : 0u},
        };
        auto fragmentShader =
            loadGeneralShader("xr_view_synthesis_pull.frag", vshadersystem::ShaderStage::eFrag, fragmentKeywords);
        if (!fragmentShader)
        {
            VULTRA_CORE_ERROR("[XrPullPyramidPass] Failed to load fragment shader");
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

    XrPushPyramidPass::XrPushPyramidPass() { setShaderProfile(rhi::ShaderProfile::eGeneral); }

    XrPullPushMipData XrPushPyramidPass::addPass(FrameGraphBuildContext&  ctx,
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

        const auto passName = makeMipPassName("XRViewSynthesisPush", lod + 1u, dstExtent);
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

                XrPullPushConstants pc {.lod = static_cast<int32_t>(lod), .depthThreshold = depthThreshold};

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

    rhi::GraphicsPipeline XrPushPyramidPass::createPipeline(const rhi::PixelFormat colorFormat,
                                                            const uint32_t         viewMask,
                                                            const bool             useDepthAware) const
    {
        auto vertexShader = loadGeneralShader("fullscreen_triangle.vert", vshadersystem::ShaderStage::eVert);
        if (!vertexShader)
        {
            VULTRA_CORE_ERROR("[XrPushPyramidPass] Failed to load vertex shader");
            return {};
        }

        rhi::ShaderLibraryRuntime::KeywordValues fragmentKeywords {
            {"USE_MULTIVIEW", viewMask != 0u ? 1u : 0u},
            {"USE_DEPTH_AWARE", useDepthAware ? 1u : 0u},
        };
        auto fragmentShader =
            loadGeneralShader("xr_view_synthesis_push.frag", vshadersystem::ShaderStage::eFrag, fragmentKeywords);
        if (!fragmentShader)
        {
            VULTRA_CORE_ERROR("[XrPushPyramidPass] Failed to load fragment shader");
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

    std::string_view XrPullPushInpaintPass::name() const { return "pull_push"; }

    FrameGraphResource XrPullPushInpaintPass::addPass(FrameGraphBuildContext&        ctx,
                                                      const FrameGraphResource       warped,
                                                      const XrViewSynthesisSettings& settings)
    {
        const auto useDepthAware  = settings.useDepthAware;
        const auto depthThreshold = std::max(settings.depthThreshold, 0.0f);
        const auto warpedDesc     = ctx.fg.getDescriptor<framegraph::FrameGraphTexture>(warped);
        const auto sizes         = makeMipSizes(warpedDesc.extent);
        const auto mipLevels     = static_cast<uint32_t>(sizes.size());
        auto       pyramidDesc   = makeInheritedTextureDesc(warpedDesc, rhi::PixelFormat::eRGBA16F);
        pyramidDesc.numMipLevels = mipLevels;
        pyramidDesc.usageFlags   = rhi::ImageUsage::eTransferDst | rhi::ImageUsage::eTransferSrc |
                                 rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled;

        struct InitData
        {
            FrameGraphResource input;
            FrameGraphResource pyramid;
        };

        const auto initData = ctx.fg.addCallbackPass<InitData>(
            "XRViewSynthesisPullPushInit",
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
                pd.pyramid =
                    builder.create<framegraph::FrameGraphTexture>("XRViewSynthesisPullPushPyramid", pyramidDesc);
                pd.pyramid = builder.write(pd.pyramid);
            },
            [](const InitData& data, FrameGraphPassResources& resources, void* ctxPtr) {
                VULTRA_SCOPED_FRAMEGRAPH_EXEC_CONTEXT(rc, ctxPtr);
                RHI_GPU_ZONE(rc.cb, "XRViewSynthesisPullPushInit");

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
            const auto pullData = m_PullPass.addPass(ctx, repaired, lod - 1u, sizes[lod - 1u], useDepthAware, depthThreshold);
            repaired            = pullData.pyramid;
            if (lod == 1u)
                return pullData.output;
        }

        return warped;
    }

    std::span<const XrViewSynthesisPass::BackendInfo> XrViewSynthesisPass::warpingBackends()
    {
        return kWarpingBackends;
    }

    std::span<const XrViewSynthesisPass::BackendInfo> XrViewSynthesisPass::inpaintingBackends()
    {
        return kInpaintingBackends;
    }

    std::span<const XrViewSynthesisPass::ViewInfo> XrViewSynthesisPass::sourceViews() { return kSourceViews; }

    std::span<const XrViewSynthesisPass::ViewInfo> XrViewSynthesisPass::targetViews() { return kTargetViews; }

    bool XrViewSynthesisPass::hasWarpingBackend(std::string_view name)
    {
        return containsBackend(kWarpingBackends, name);
    }

    bool XrViewSynthesisPass::hasInpaintingBackend(std::string_view name)
    {
        return containsBackend(kInpaintingBackends, name);
    }

    FrameGraphResource XrViewSynthesisPass::addPass(FrameGraphBuildContext&        ctx,
                                                    const FrameGraphResource       source,
                                                    const FrameGraphResource       depth,
                                                    const XrViewSynthesisSettings& settings)
    {
        if (!settings.enabled)
            return source;

        const auto warpingBackend = resolveWarpingBackend(settings.warpingBackend);
        if (warpingBackend == "none")
            return source;

        auto warped = m_GeometryWarpPass.addPass(ctx, source, depth, settings);

        const auto inpaintingBackend = resolveInpaintingBackend(settings.inpaintingBackend);
        if (inpaintingBackend == "none")
            return warped;

        return m_PullPushInpaintPass.addPass(ctx, warped, settings);
    }
} // namespace vultra
