#include "vultra/function/rendering/srp/builtin/passes/geometry_warp_pass.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/structs/pixel_format.hpp"
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
#include <cctype>
#include <string>

namespace vultra
{
    namespace
    {
        constexpr auto GEOMETRY_WARP_PASS_NAME = "GeometryWarp";

        enum class SynthesisView : uint32_t
        {
            ePrimary = 0,
            eLeft    = 1,
            eRight   = 2,
            eStereo  = 3,
        };

        struct GeometryWarpPushConstants
        {
            glm::vec2 resolution {};
            uint32_t  sourceView {static_cast<uint32_t>(SynthesisView::eLeft)};
            uint32_t  targetView {static_cast<uint32_t>(SynthesisView::eRight)};
            uint32_t  gridSize {4};
            float     sideLenThreshold {0.05f};
            uint32_t  useDepthAware {1u};
        };

        // Matches WarpBlock in geometry_warp.vert (std140 UBO).
        struct WarpBlock
        {
            glm::mat4 sourceInvViewProj {1.0f};
            glm::mat4 targetViewProj[2] {glm::mat4 {1.0f}, glm::mat4 {1.0f}};
        };

        [[nodiscard]] uint32_t divRoundUp(const uint32_t x, const uint32_t y) { return y == 0u ? x : (x + y - 1u) / y; }

        [[nodiscard]] std::string normalizeName(std::string_view text)
        {
            std::string out;
            out.reserve(text.size());
            for (const char ch : text)
                out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
            return out;
        }
    } // namespace

    GeometryWarpPass::GeometryWarpPass() { setShaderProfile(rhi::ShaderProfile::eGeneral); }

    FrameGraphResource GeometryWarpPass::addPass(FrameGraphBuildContext&      ctx,
                                                 const FrameGraphResource     source,
                                                 const FrameGraphResource     depth,
                                                 const ViewSynthesisSettings& settings)
    {
        const auto sourceDesc   = ctx.fg.getDescriptor<framegraph::FrameGraphTexture>(source);
        const auto depthDesc    = ctx.fg.getDescriptor<framegraph::FrameGraphTexture>(depth);
        const auto gridSize     = std::max(settings.gridSize, 1u);
        const auto cellsX       = std::max(1u, divRoundUp(sourceDesc.extent.width, gridSize));
        const auto cellsY       = std::max(1u, divRoundUp(sourceDesc.extent.height, gridSize));
        const auto vertexCount  = cellsX * cellsY * 6u;
        const auto sourceView   = normalizeName(settings.sourceView);
        const auto targetView   = normalizeName(settings.targetView);
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

        WarpBlock           warpBlock {};
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
                    "[GeometryWarp] No stereo cameras available; warp passes source through (mono/preview).");
            }
        }

        const auto warpBlockResource = uploadFrameGraphStruct(ctx.fg,
                                                              ctx.frameResources,
                                                              ctx.rd,
                                                              "UploadWarpBlock",
                                                              "WarpBlock",
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
                    "GeometryWarpColor", makeInheritedTextureDesc(sourceDesc, rhi::PixelFormat::eRGBA16F));
                pd.warped = builder.write(pd.warped,
                                          framegraph::Attachment {
                                              .index       = 0,
                                              .imageAspect = rhi::ImageAspect::eColor,
                                              // Uncovered pixels stay alpha=1 (hole) for the pull-push stage.
                                              .clearValue  = framegraph::ClearValue::eOpaqueBlack,
                                          });

                auto depthOutputDesc       = makeInheritedTextureDesc(sourceDesc, rhi::PixelFormat::eDepth32F);
                depthOutputDesc.usageFlags = rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled;
                pd.warpedDepth = builder.create<framegraph::FrameGraphTexture>("GeometryWarpDepth", depthOutputDesc);
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

                GeometryWarpPushConstants pc {
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

    rhi::GraphicsPipeline GeometryWarpPass::createPipeline(const rhi::PixelFormat colorFormat,
                                                           const uint32_t         viewMask) const
    {
        rhi::ShaderLibraryRuntime::KeywordValues keywords {
            {"USE_MULTIVIEW", viewMask != 0u ? 1u : 0u},
        };
        auto vertexShader = loadGeneralShader("geometry_warp.vert", vshadersystem::ShaderStage::eVert, keywords);
        if (!vertexShader)
        {
            VULTRA_CORE_ERROR("[GeometryWarpPass] Failed to load vertex shader");
            return {};
        }

        auto geometryShader = loadGeneralShader("geometry_warp.geom", vshadersystem::ShaderStage::eGeom, keywords);
        if (!geometryShader)
        {
            // Geometry shaders are unavailable on the WebGPU/compatibility profile
            // (warn-skipped by the toolchain). Select a different warp backend there.
            VULTRA_CORE_ERROR("[GeometryWarpPass] Failed to load geometry shader (unsupported on this profile?)");
            return {};
        }

        auto fragmentShader = loadGeneralShader("geometry_warp.frag", vshadersystem::ShaderStage::eFrag, keywords);
        if (!fragmentShader)
        {
            VULTRA_CORE_ERROR("[GeometryWarpPass] Failed to load fragment shader");
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
} // namespace vultra
