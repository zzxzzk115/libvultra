#include "vultra/function/rendering/srp/builtin/passes/xr_view_synthesis_pass.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/structs/geometry_info.hpp"
#include "vultra/core/rhi/structs/pixel_format.hpp"
#include "vultra/core/rhi/texture.hpp"
#include "vultra/function/framegraph/framegraph_buffer.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"
#include "vultra/function/rendering/srp/render_target_desc.hpp"

#include <fg/FrameGraph.hpp>
#include <glm/ext/vector_float2.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <vector>

namespace vultra
{
    namespace
    {
        constexpr auto BUILD_PASS_NAME   = "XRViewSynthesisAdaptiveMeshBuild";
        constexpr auto RASTER_PASS_NAME  = "XRViewSynthesisAdaptiveMeshRaster";
        constexpr auto INPAINT_PASS_NAME = "XRViewSynthesisInpaint";

        constexpr std::array<XrViewSynthesisPass::BackendInfo, 2> kWarpingBackends {{
            {"adaptive_mesh_graphics", "Adaptive Mesh", "Compute-generated adaptive screen-space mesh plus graphics rasterization."},
            {"none", "None", "Forward source color without warping."},
        }};

        constexpr std::array<XrViewSynthesisPass::BackendInfo, 2> kInpaintingBackends {{
            {"pull_push", "Pull Push", "Pull-push style hole repair using the warping alpha validity convention."},
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

        struct XrAdaptiveMeshVertexGpu
        {
            glm::vec4 uvDepthValid {};
            glm::vec4 color {};
        };

        struct XrAdaptiveMeshBuildPushConstants
        {
            glm::vec2 resolution {};
            uint32_t  sourceView {static_cast<uint32_t>(XrSynthesisView::eLeft)};
            uint32_t  targetView {static_cast<uint32_t>(XrSynthesisView::eRight)};
            uint32_t  baseGridSize {16};
            uint32_t  maxSubdivision {3};
            float     sideLengthThreshold {0.1f};
            float     depthThreshold {0.015f};
            uint32_t  cellsX {1};
            uint32_t  cellsY {1};
            uint32_t  maxSubdiv {1};
            uint32_t  vertexCapacity {0};
        };

        struct XrAdaptiveMeshRasterPushConstants
        {
            glm::vec2 resolution {};
            uint32_t  sourceView {static_cast<uint32_t>(XrSynthesisView::eLeft)};
            uint32_t  targetView {static_cast<uint32_t>(XrSynthesisView::eRight)};
        };

        struct XrPullPushConstants
        {
            int32_t lod {0};
        };

        [[nodiscard]] uint32_t divRoundUp(const uint32_t x, const uint32_t y)
        {
            return y == 0u ? x : (x + y - 1u) / y;
        }

        [[nodiscard]] uint32_t maxSubdivFromSettings(const XrViewSynthesisSettings& settings)
        {
            return 1u << std::min(settings.maxSubdivision, 4u);
        }

        [[nodiscard]] std::vector<rhi::Extent2D> makeMipSizes(const rhi::Extent2D extent)
        {
            std::vector<rhi::Extent2D> sizes;
            const uint32_t mipLevelCount = rhi::calcMipLevels(extent);
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

        [[nodiscard]] std::string makeMipPassName(std::string_view prefix,
                                                  const uint32_t   lod,
                                                  const rhi::Extent2D extent)
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

        [[nodiscard]] XrSynthesisView parseView(std::string_view text, const XrSynthesisView fallback)
        {
            const auto value = normalizeName(text);
            if (value == "left")
                return XrSynthesisView::eLeft;
            if (value == "right")
                return XrSynthesisView::eRight;
            if (value == "primary")
                return XrSynthesisView::ePrimary;
            if (value == "stereo")
                return XrSynthesisView::eStereo;
            return fallback;
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
            warnUnknownBackendOnce("warping", requested, "adaptive_mesh_graphics");
            return "adaptive_mesh_graphics";
        }

        [[nodiscard]] std::string_view resolveInpaintingBackend(std::string_view requested)
        {
            if (containsBackend(kInpaintingBackends, requested))
                return requested;
            warnUnknownBackendOnce("inpainting", requested, "pull_push");
            return "pull_push";
        }
    } // namespace

    XrAdaptiveMeshBuildPass::XrAdaptiveMeshBuildPass() { setShaderProfile(rhi::ShaderProfile::eGeneral); }

    XrAdaptiveMeshData XrAdaptiveMeshBuildPass::addPass(FrameGraphBuildContext&            ctx,
                                                        const FrameGraphResource           source,
                                                        const FrameGraphResource           depth,
                                                        const XrViewSynthesisSettings& settings)
    {
        const auto sourceDesc = ctx.fg.getDescriptor<framegraph::FrameGraphTexture>(source);
        const auto depthDesc  = ctx.fg.getDescriptor<framegraph::FrameGraphTexture>(depth);

        const uint32_t baseGridSize = std::max(settings.baseGridSize, 1u);
        const uint32_t cellsX       = std::max(1u, divRoundUp(sourceDesc.extent.width, baseGridSize));
        const uint32_t cellsY       = std::max(1u, divRoundUp(sourceDesc.extent.height, baseGridSize));
        const uint32_t maxSubdiv    = maxSubdivFromSettings(settings);
        const uint32_t vertexCount  = cellsX * cellsY * maxSubdiv * maxSubdiv * 6u;

        struct PassData
        {
            FrameGraphResource source;
            FrameGraphResource depth;
            FrameGraphResource vertices;
        };

        const auto data = ctx.fg.addCallbackPass<PassData>(
            BUILD_PASS_NAME,
            [source, depth, depthDesc, vertexCount](FrameGraph::Builder& builder, PassData& pd) {
                PASS_SETUP_ZONE;

                pd.source = builder.read(source,
                                         framegraph::TextureRead {
                                             .binding =
                                                 {
                                                     .location      = {.set = 3, .binding = 0},
                                                     .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                                 },
                                             .type        = framegraph::TextureRead::Type::eCombinedImageSampler,
                                             .imageAspect = rhi::ImageAspect::eColor,
                                         });
                pd.depth = builder.read(depth,
                                        framegraph::TextureRead {
                                            .binding =
                                                {
                                                    .location      = {.set = 3, .binding = 1},
                                                    .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                                },
                                            .type        = framegraph::TextureRead::Type::eCombinedImageSampler,
                                            .imageAspect = depthDesc.format == rhi::PixelFormat::eDepth32F ?
                                                               rhi::ImageAspect::eDepth :
                                                               rhi::ImageAspect::eColor,
                                        });

                pd.vertices = builder.create<framegraph::FrameGraphBuffer>(
                    "XRViewSynthesisAdaptiveMeshVertices",
                    {
                        .type     = framegraph::BufferType::eStorageBuffer,
                        .stride   = sizeof(XrAdaptiveMeshVertexGpu),
                        .capacity = std::max(vertexCount, 1u),
                    });
                pd.vertices = builder.write(pd.vertices,
                                            framegraph::BindingInfo {
                                                .location      = {.set = 3, .binding = 2},
                                                .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                            });
            },
            [this, sourceDesc, settings, cellsX, cellsY, maxSubdiv, vertexCount](
                const PassData&, FrameGraphPassResources&, void* ctxPtr) {
                VULTRA_SCOPED_FRAMEGRAPH_EXEC_CONTEXT(rc, ctxPtr);
                setRenderDevice(rc.rd);
                if (!rc.ext.builtinShaderLib)
                    return;
                setShaderLib(*rc.ext.builtinShaderLib);

                RHI_GPU_ZONE(rc.cb, BUILD_PASS_NAME);

                rhi::ShaderLibraryRuntime::KeywordValues keywords {
                    {"USE_MULTIVIEW", sourceDesc.viewMask != 0u || sourceDesc.layers > 1u ? 1u : 0u},
                };
                const auto variantHash = computeGeneralVariantHash(
                    "xr_view_synthesis_adaptive_mesh_build.comp", vshadersystem::ShaderStage::eComp, keywords);
                const auto* pipeline = getPipeline(variantHash);
                if (!pipeline)
                    return;

                XrAdaptiveMeshBuildPushConstants pc {
                    .resolution          = glm::vec2(static_cast<float>(sourceDesc.extent.width),
                                            static_cast<float>(sourceDesc.extent.height)),
                    .sourceView          = static_cast<uint32_t>(parseView(settings.sourceView, XrSynthesisView::eLeft)),
                    .targetView          = static_cast<uint32_t>(parseView(settings.targetView, XrSynthesisView::eRight)),
                    .baseGridSize        = std::max(settings.baseGridSize, 1u),
                    .maxSubdivision      = settings.maxSubdivision,
                    .sideLengthThreshold = std::max(settings.sideLengthThreshold, 0.0f),
                    .depthThreshold      = std::max(settings.depthThreshold, 0.0f),
                    .cellsX              = cellsX,
                    .cellsY              = cellsY,
                    .maxSubdiv           = maxSubdiv,
                    .vertexCapacity      = vertexCount,
                };

                rc.overrideSampler(rc.resourceSet[3][0], rc.ext.samplers["bilinear"]);
                rc.overrideSampler(rc.resourceSet[3][1], rc.ext.samplers["point"]);
                rc.cb.bindPipeline(*pipeline);
                rc.bindDescriptorSets(*pipeline);
                rc.cb.pushConstants(rhi::ShaderStages::eCompute, 0, &pc);
                rc.cb.dispatch({divRoundUp(cellsX, 8u), divRoundUp(cellsY, 8u), 1u});
            });

        return {.vertices = data.vertices, .vertexCount = vertexCount};
    }

    rhi::ComputePipeline XrAdaptiveMeshBuildPass::createPipeline(const uint64_t variantHash) const
    {
        auto shader = loadGeneralShaderVariant(variantHash, vshadersystem::ShaderStage::eComp);
        if (!shader)
        {
            VULTRA_CORE_ERROR("[XrAdaptiveMeshBuildPass] Failed to load compute shader variant");
            return {};
        }
        return getRenderDevice().createComputePipelineBuiltin(*shader);
    }

    XrAdaptiveMeshRasterPass::XrAdaptiveMeshRasterPass() { setShaderProfile(rhi::ShaderProfile::eGeneral); }

    FrameGraphResource XrAdaptiveMeshRasterPass::addPass(FrameGraphBuildContext&            ctx,
                                                         const XrAdaptiveMeshData&          mesh,
                                                         const FrameGraphResource           source,
                                                         const XrViewSynthesisSettings& settings)
    {
        const auto sourceDesc = ctx.fg.getDescriptor<framegraph::FrameGraphTexture>(source);

        struct PassData
        {
            FrameGraphResource vertices;
            FrameGraphResource source;
            FrameGraphResource warped;
        };

        const auto data = ctx.fg.addCallbackPass<PassData>(
            RASTER_PASS_NAME,
            [mesh, source, outputDesc = makeInheritedTextureDesc(sourceDesc, rhi::PixelFormat::eRGBA16F)](
                FrameGraph::Builder& builder, PassData& pd) {
                PASS_SETUP_ZONE;

                pd.vertices = builder.read(mesh.vertices,
                                           framegraph::BindingInfo {
                                               .location      = {.set = 3, .binding = 0},
                                               .pipelineStage = framegraph::PipelineStage::eVertexShader,
                                           });
                pd.source = builder.read(source,
                                         framegraph::TextureRead {
                                             .binding =
                                                 {
                                                     .location      = {.set = 3, .binding = 1},
                                                     .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                                 },
                                             .type        = framegraph::TextureRead::Type::eCombinedImageSampler,
                                             .imageAspect = rhi::ImageAspect::eColor,
                                         });

                pd.warped = builder.create<framegraph::FrameGraphTexture>("XRViewSynthesisWarped", outputDesc);
                pd.warped = builder.write(pd.warped,
                                          framegraph::Attachment {
                                              .index       = 0,
                                              .imageAspect = rhi::ImageAspect::eColor,
                                              .clearValue  = framegraph::ClearValue::eTransparentBlack,
                                          });
            },
            [this, sourceDesc, settings, vertexCount = mesh.vertexCount](
                const PassData&, FrameGraphPassResources&, void* ctxPtr) {
                VULTRA_SCOPED_FRAMEGRAPH_EXEC_CONTEXT(rc, ctxPtr);
                setRenderDevice(rc.rd);
                if (!rc.ext.builtinShaderLib)
                    return;
                setShaderLib(*rc.ext.builtinShaderLib);

                RHI_GPU_ZONE(rc.cb, RASTER_PASS_NAME);

                assert(rc.framebufferInfo().has_value());
                const auto framebufferInfo = rc.framebufferInfo().value();
                const auto* pipeline = getPipeline(rhi::getColorFormat(framebufferInfo, 0), framebufferInfo.viewMask);
                if (!pipeline)
                    return;

                XrAdaptiveMeshRasterPushConstants pc {
                    .resolution = glm::vec2(static_cast<float>(sourceDesc.extent.width),
                                            static_cast<float>(sourceDesc.extent.height)),
                    .sourceView = static_cast<uint32_t>(parseView(settings.sourceView, XrSynthesisView::eLeft)),
                    .targetView = static_cast<uint32_t>(parseView(settings.targetView, XrSynthesisView::eRight)),
                };

                rc.cb.bindPipeline(*pipeline);
                rc.bindDescriptorSets(*pipeline);
                rc.cb.pushConstants(rhi::ShaderStages::eVertex | rhi::ShaderStages::eFragment, 0, &pc);
                rc.cb.beginRendering(framebufferInfo)
                    .draw({.topology = rhi::PrimitiveTopology::eTriangleList, .numVertices = vertexCount})
                    .endRendering();
            });

        return data.warped;
    }

    rhi::GraphicsPipeline XrAdaptiveMeshRasterPass::createPipeline(const rhi::PixelFormat colorFormat,
                                                                   const uint32_t         viewMask) const
    {
        rhi::ShaderLibraryRuntime::KeywordValues keywords {
            {"USE_MULTIVIEW", viewMask != 0u ? 1u : 0u},
        };
        auto vertexShader =
            loadGeneralShader("xr_view_synthesis_adaptive_mesh_raster.vert", vshadersystem::ShaderStage::eVert, keywords);
        if (!vertexShader)
        {
            VULTRA_CORE_ERROR("[XrAdaptiveMeshRasterPass] Failed to load vertex shader");
            return {};
        }

        auto fragmentShader = loadGeneralShader(
            "xr_view_synthesis_adaptive_mesh_raster.frag", vshadersystem::ShaderStage::eFrag, keywords);
        if (!fragmentShader)
        {
            VULTRA_CORE_ERROR("[XrAdaptiveMeshRasterPass] Failed to load fragment shader");
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

    XrPullPyramidPass::XrPullPyramidPass() { setShaderProfile(rhi::ShaderProfile::eGeneral); }

    XrPullPushMipData XrPullPyramidPass::addPass(FrameGraphBuildContext&      ctx,
                                                 const FrameGraphResource     pyramid,
                                                 const uint32_t               lod,
                                                 const rhi::Extent2D          dstExtent)
    {
        const auto pyramidDesc = ctx.fg.getDescriptor<framegraph::FrameGraphTexture>(pyramid);

        struct PassData
        {
            FrameGraphResource pyramid;
            FrameGraphResource output;
        };

        const auto passName = makeMipPassName("XRViewSynthesisPull", lod, dstExtent);
        const auto data = ctx.fg.addCallbackPass<PassData>(
            passName,
            [pyramid, outputDesc = makeInheritedTextureDesc(pyramidDesc, rhi::PixelFormat::eRGBA16F), lod, dstExtent,
             outputName = passName + " Output"](
                FrameGraph::Builder& builder, PassData& pd) {
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

                auto desc  = outputDesc;
                desc.extent = dstExtent;
                desc.numMipLevels = 1u;
                pd.output = builder.create<framegraph::FrameGraphTexture>(outputName, desc);
                pd.output = builder.write(pd.output,
                                          framegraph::Attachment {
                                                  .index       = 0,
                                                  .imageAspect = rhi::ImageAspect::eColor,
                                                  .clearValue  = framegraph::ClearValue::eTransparentBlack,
                                              });
                pd.pyramid = builder.write(pyramid);
            },
            [this, lod, passName](const PassData& data, FrameGraphPassResources& resources, void* ctxPtr) {
                VULTRA_SCOPED_FRAMEGRAPH_EXEC_CONTEXT(rc, ctxPtr);
                setRenderDevice(rc.rd);
                if (!rc.ext.builtinShaderLib)
                    return;
                setShaderLib(*rc.ext.builtinShaderLib);

                RHI_GPU_ZONE(rc.cb, passName.c_str());

                assert(rc.framebufferInfo().has_value());
                const auto framebufferInfo = rc.framebufferInfo().value();
                const auto* pipeline = getPipeline(rhi::getColorFormat(framebufferInfo, 0), framebufferInfo.viewMask);
                if (!pipeline)
                    return;

                XrPullPushConstants pc {
                    .lod = static_cast<int32_t>(lod),
                };

                auto& pyramidTexture = *resources.get<framegraph::FrameGraphTexture>(data.pyramid).texture;
                rhi::prepareForReading(rc.cb, pyramidTexture, lod);
                rhi::prepareForReading(rc.cb, pyramidTexture, lod + 1u);
                rc.overrideSampler(rc.resourceSet[3][0], rc.ext.samplers["bilinear"]);
                rc.overrideSampler(rc.resourceSet[3][1], rc.ext.samplers["point"]);
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
                                                            const uint32_t         viewMask) const
    {
        auto vertexShader = loadGeneralShader("fullscreen_triangle.vert", vshadersystem::ShaderStage::eVert);
        if (!vertexShader)
        {
            VULTRA_CORE_ERROR("[XrPullPyramidPass] Failed to load vertex shader");
            return {};
        }

        rhi::ShaderLibraryRuntime::KeywordValues fragmentKeywords {
            {"USE_MULTIVIEW", viewMask != 0u ? 1u : 0u},
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

    XrPullPushMipData XrPushPyramidPass::addPass(FrameGraphBuildContext&      ctx,
                                                 const FrameGraphResource     pyramid,
                                                 const uint32_t               lod,
                                                 const rhi::Extent2D          dstExtent)
    {
        const auto pyramidDesc = ctx.fg.getDescriptor<framegraph::FrameGraphTexture>(pyramid);

        struct PassData
        {
            FrameGraphResource pyramid;
            FrameGraphResource output;
        };

        const auto passName = makeMipPassName("XRViewSynthesisPush", lod + 1u, dstExtent);
        const auto data = ctx.fg.addCallbackPass<PassData>(
            passName,
            [pyramid, outputDesc = makeInheritedTextureDesc(pyramidDesc, rhi::PixelFormat::eRGBA16F), dstExtent,
             outputName = passName + " Output"](
                FrameGraph::Builder& builder, PassData& pd) {
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

                auto desc  = outputDesc;
                desc.extent = dstExtent;
                desc.numMipLevels = 1u;
                pd.output = builder.create<framegraph::FrameGraphTexture>(outputName, desc);
                pd.output = builder.write(pd.output,
                                          framegraph::Attachment {
                                              .index       = 0,
                                              .imageAspect = rhi::ImageAspect::eColor,
                                              .clearValue  = framegraph::ClearValue::eTransparentBlack,
                                          });
                pd.pyramid = builder.write(pyramid);
            },
            [this, lod, passName](const PassData& data, FrameGraphPassResources& resources, void* ctxPtr) {
                VULTRA_SCOPED_FRAMEGRAPH_EXEC_CONTEXT(rc, ctxPtr);
                setRenderDevice(rc.rd);
                if (!rc.ext.builtinShaderLib)
                    return;
                setShaderLib(*rc.ext.builtinShaderLib);

                RHI_GPU_ZONE(rc.cb, passName.c_str());

                assert(rc.framebufferInfo().has_value());
                const auto framebufferInfo = rc.framebufferInfo().value();
                const auto* pipeline = getPipeline(rhi::getColorFormat(framebufferInfo, 0), framebufferInfo.viewMask);
                if (!pipeline)
                    return;

                XrPullPushConstants pc {
                    .lod = static_cast<int32_t>(lod),
                };

                auto& pyramidTexture = *resources.get<framegraph::FrameGraphTexture>(data.pyramid).texture;
                rhi::prepareForReading(rc.cb, pyramidTexture, lod);
                rc.overrideSampler(rc.resourceSet[3][0], rc.ext.samplers["point"]);
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
                                                            const uint32_t         viewMask) const
    {
        auto vertexShader = loadGeneralShader("fullscreen_triangle.vert", vshadersystem::ShaderStage::eVert);
        if (!vertexShader)
        {
            VULTRA_CORE_ERROR("[XrPushPyramidPass] Failed to load vertex shader");
            return {};
        }

        rhi::ShaderLibraryRuntime::KeywordValues fragmentKeywords {
            {"USE_MULTIVIEW", viewMask != 0u ? 1u : 0u},
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

    FrameGraphResource XrPullPushInpaintPass::addPass(FrameGraphBuildContext&            ctx,
                                                      const FrameGraphResource           warped,
                                                      const XrViewSynthesisSettings&)
    {
        const auto warpedDesc = ctx.fg.getDescriptor<framegraph::FrameGraphTexture>(warped);
        const auto sizes      = makeMipSizes(warpedDesc.extent);
        const auto mipLevels  = static_cast<uint32_t>(sizes.size());
        auto       pyramidDesc = makeInheritedTextureDesc(warpedDesc, rhi::PixelFormat::eRGBA16F);
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
                pd.pyramid = builder.create<framegraph::FrameGraphTexture>("XRViewSynthesisPullPushPyramid", pyramidDesc);
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
            pyramid = m_PushPass.addPass(ctx, pyramid, lod, sizes[lod + 1u]).pyramid;

        FrameGraphResource repaired = pyramid;
        for (uint32_t lod = mipLevels - 1u; lod > 0u; --lod)
        {
            const auto pullData = m_PullPass.addPass(ctx, repaired, lod - 1u, sizes[lod - 1u]);
            repaired = pullData.pyramid;
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

    std::span<const XrViewSynthesisPass::ViewInfo> XrViewSynthesisPass::sourceViews()
    {
        return kSourceViews;
    }

    std::span<const XrViewSynthesisPass::ViewInfo> XrViewSynthesisPass::targetViews()
    {
        return kTargetViews;
    }

    bool XrViewSynthesisPass::hasWarpingBackend(std::string_view name)
    {
        return containsBackend(kWarpingBackends, name);
    }

    bool XrViewSynthesisPass::hasInpaintingBackend(std::string_view name)
    {
        return containsBackend(kInpaintingBackends, name);
    }

    FrameGraphResource XrViewSynthesisPass::addPass(FrameGraphBuildContext&            ctx,
                                                    const FrameGraphResource           source,
                                                    const FrameGraphResource           depth,
                                                    const XrViewSynthesisSettings& settings)
    {
        const auto warpingBackend = resolveWarpingBackend(settings.warpingBackend);
        if (warpingBackend == "none")
            return source;

        const auto mesh   = m_AdaptiveMeshBuildPass.addPass(ctx, source, depth, settings);
        auto       warped = m_AdaptiveMeshRasterPass.addPass(ctx, mesh, source, settings);

        const auto inpaintingBackend = resolveInpaintingBackend(settings.inpaintingBackend);
        if (inpaintingBackend == "none")
            return warped;

        return m_PullPushInpaintPass.addPass(ctx, warped, settings);
    }
} // namespace vultra
