#include "vultra/function/rendering/srp/builtin/passes/gaussian_splat_render_pass.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/function/framegraph/framegraph_buffer.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"

#include <fg/FrameGraph.hpp>

#include <cmath>
#include <optional>
#include <vector>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace vultra
{
    namespace
    {
        constexpr auto PASS_NAME = "GaussianSplatRenderPass";

        // Use higher-precision intermediate to reduce quantization/banding before final composition.
        constexpr auto  kColorFormat     = rhi::PixelFormat::eRGBA16F;
        constexpr float kViewZNearReject = -0.02f;
        constexpr float kTileSizePx      = 16.0f;

        [[nodiscard]] float extractMaxScale(const glm::mat4& model)
        {
            const glm::vec3 x = glm::vec3(model[0]);
            const glm::vec3 y = glm::vec3(model[1]);
            const glm::vec3 z = glm::vec3(model[2]);
            return glm::max(glm::length(x), glm::max(glm::length(y), glm::length(z)));
        }

        [[nodiscard]] std::optional<rhi::Rect2D> buildDrawTileScissor(const RenderCamera&               camera,
                                                                      const rhi::Extent2D               extent,
                                                                      const resource::GpuGaussianSplat& splat,
                                                                      const glm::mat4&                  model)
        {
            if (extent.width == 0u || extent.height == 0u)
                return std::nullopt;

            const glm::vec4 worldCenter4 = model * glm::vec4(splat.center, 1.0f);
            const glm::vec4 viewCenter4  = camera.view * worldCenter4;
            const glm::vec3 viewCenter   = glm::vec3(viewCenter4);

            if (viewCenter.z >= kViewZNearReject)
                return std::nullopt;

            const float depth = glm::max(-viewCenter.z, 1e-4f);
            const float ndcX  = (camera.projection[0][0] * viewCenter.x) / depth;
            const float ndcY  = (camera.projection[1][1] * viewCenter.y) / depth;

            const float widthF    = static_cast<float>(extent.width);
            const float heightF   = static_cast<float>(extent.height);
            const float centerPxX = (ndcX * 0.5f + 0.5f) * widthF;
            const float centerPxY = (ndcY * 0.5f + 0.5f) * heightF;

            const float pxPerWorldX = 0.5f * widthF * std::abs(camera.projection[0][0]) / depth;
            const float pxPerWorldY = 0.5f * heightF * std::abs(camera.projection[1][1]) / depth;
            const float pxPerWorld  = glm::max(pxPerWorldX, pxPerWorldY);

            const float radiusWS = glm::max(splat.radius * extractMaxScale(model), 1e-4f);
            const float radiusPx = radiusWS * pxPerWorld + (2.0f * kTileSizePx);

            float minX = centerPxX - radiusPx;
            float minY = centerPxY - radiusPx;
            float maxX = centerPxX + radiusPx;
            float maxY = centerPxY + radiusPx;

            minX = std::floor(minX / kTileSizePx) * kTileSizePx;
            minY = std::floor(minY / kTileSizePx) * kTileSizePx;
            maxX = std::ceil(maxX / kTileSizePx) * kTileSizePx;
            maxY = std::ceil(maxY / kTileSizePx) * kTileSizePx;

            minX = glm::clamp(minX, 0.0f, widthF);
            minY = glm::clamp(minY, 0.0f, heightF);
            maxX = glm::clamp(maxX, 0.0f, widthF);
            maxY = glm::clamp(maxY, 0.0f, heightF);

            const int32_t ix0 = static_cast<int32_t>(minX);
            const int32_t iy0 = static_cast<int32_t>(minY);
            const int32_t ix1 = static_cast<int32_t>(maxX);
            const int32_t iy1 = static_cast<int32_t>(maxY);

            if (ix1 <= ix0 || iy1 <= iy0)
                return std::nullopt;

            return rhi::Rect2D {
                .offset = {.x = ix0, .y = iy0},
                .extent = {.width = static_cast<uint32_t>(ix1 - ix0), .height = static_cast<uint32_t>(iy1 - iy0)},
            };
        }
    } // namespace

    FrameGraphResource GaussianSplatRenderPass::addPass(FrameGraphBuildContext& ctx, FrameGraphResource buildToken)
    {
        struct PassData
        {
            FrameGraphResource camera;
            FrameGraphResource buildToken;
            FrameGraphResource color;
            FrameGraphResource depth;
        };

        const auto resolution  = ctx.view().extent;
        const auto cameraBlock = ctx.bb.get<CameraData>().cameraBlock.fgResource;

        auto data = ctx.fg.addCallbackPass<PassData>(
            PASS_NAME,
            [buildToken, cameraBlock, resolution](FrameGraph::Builder& builder, PassData& pd) {
                PASS_SETUP_ZONE;

                pd.camera = builder.read(cameraBlock,
                                         framegraph::BindingInfo {
                                             .location      = {.set = 0, .binding = 0},
                                             .pipelineStage = framegraph::PipelineStage::eVertexShader,
                                         });

                pd.buildToken = buildToken;
                if (pd.buildToken)
                {
                    pd.buildToken = builder.read(pd.buildToken,
                                                 framegraph::BindingInfo {
                                                     .location      = {},
                                                     .pipelineStage = framegraph::PipelineStage::eTransfer,
                                                 });
                }

                pd.color = builder.create<framegraph::FrameGraphTexture>(
                    "GaussianSplatColor",
                    {
                        .extent     = resolution,
                        .format     = kColorFormat,
                        .usageFlags = rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled,
                    });
                pd.color = builder.write(pd.color,
                                         framegraph::Attachment {
                                             .index       = 0,
                                             .imageAspect = rhi::ImageAspect::eColor,
                                             .clearValue  = framegraph::ClearValue::eTransparentBlack,
                                         });

                pd.depth = builder.create<framegraph::FrameGraphTexture>(
                    "GaussianSplatDepth",
                    {
                        .extent     = resolution,
                        .format     = rhi::PixelFormat::eDepth32F,
                        .usageFlags = rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled,
                    });
                pd.depth = builder.write(pd.depth,
                                         framegraph::Attachment {
                                             .imageAspect = rhi::ImageAspect::eDepth,
                                             .clearValue  = framegraph::ClearValue::eOne,
                                         });
            },
            [this](const PassData& pd, FrameGraphPassResources& resources, void* ctxPtr) {
                auto& rc = *static_cast<FrameGraphExecContext*>(ctxPtr);
                setRenderDevice(rc.rd);
                RHI_GPU_ZONE(rc.cb, PASS_NAME);

                auto* gpuSceneView     = rc.view().gpuSceneView;
                auto* gpuSceneDatabase = rc.view().gpuSceneDatabase;
                auto* cameraUbo        = resources.get<framegraph::FrameGraphBuffer>(pd.camera).buffer;

                assert(rc.framebufferInfo().has_value());
                rc.cb.beginRendering(rc.framebufferInfo().value());

                const bool canDraw = rc.ext.builtinShaderLib && gpuSceneView && cameraUbo && gpuSceneDatabase &&
                                     gpuSceneDatabase->resources && gpuSceneView->gaussianSplatDrawBuffer &&
                                     gpuSceneView->gaussianSplatProjectedBuffer &&
                                     gpuSceneView->gaussianSplatSortValuesBuffer &&
                                     gpuSceneView->gaussianSplatIndirectBuffer.has_value() &&
                                     gpuSceneView->maxGaussianSplatSortElements > 0u;

                static bool s_LoggedGaussianSplatRender = false;
                if (!s_LoggedGaussianSplatRender)
                {
                    VULTRA_CORE_INFO("[GaussianSplat] render canDraw={} draws={} dispatchable={} hasProjected={} "
                                     "hasValues={}",
                                     canDraw,
                                     gpuSceneView ? gpuSceneView->gaussianSplatDraws.size() : 0u,
                                     gpuSceneView ? gpuSceneView->getDispatchableGaussianSplatDrawCount() : 0u,
                                     gpuSceneView && gpuSceneView->gaussianSplatProjectedBuffer ? 1u : 0u,
                                     gpuSceneView && gpuSceneView->gaussianSplatSortValuesBuffer ? 1u : 0u);
                    s_LoggedGaussianSplatRender = true;
                }

                if (canDraw)
                {
                    setShaderLib(*rc.ext.builtinShaderLib);

                    auto           variantHash =
                        getShaderLib().computeVariantHash("gaussian_splat.vert", vshadersystem::ShaderStage::eVert, {});
                    const auto* pipeline = getPipeline(variantHash);

                    if (pipeline)
                    {
                        // Sort values and projected splats are written by the compute cull pass and
                        // read by the vertex shader here — use vertex-stage barrier to synchronize.
                        rhi::prepareForReading(rc.cb, *gpuSceneView->gaussianSplatSortValuesBuffer);
                        rhi::prepareForReading(rc.cb, *gpuSceneView->gaussianSplatProjectedBuffer);

                        rc.cb.bindPipeline(*pipeline);
                        rc.resourceSet[0] = {
                            {0, rhi::bindings::UniformBuffer {.buffer = cameraUbo}},
                            {18,
                             rhi::bindings::StorageBuffer {.buffer =
                                                               gpuSceneView->gaussianSplatSortValuesBuffer.get()}},
                            {19,
                             rhi::bindings::StorageBuffer {.buffer = gpuSceneView->gaussianSplatProjectedBuffer.get()}},
                        };
                        rc.bindDescriptorSets(*pipeline);
                        rc.cb.drawIndirect(rhi::DrawIndirectInfo {
                            .buffer       = &gpuSceneView->gaussianSplatIndirectBuffer.value(),
                            .firstCommand = 0,
                            .commandCount = 1,
                        });
                    }
                }

                rc.cb.endRendering();
                rc.clear();
            });

        return data.color;
    }

    rhi::GraphicsPipeline GaussianSplatRenderPass::createPipeline(uint64_t variantHash) const
    {
        auto vertexShader = getShaderLib().load(variantHash, vshadersystem::ShaderStage::eVert);
        if (!vertexShader)
        {
            VULTRA_CORE_ERROR("[GaussianSplatRenderPass] Failed to load vertex shader variant");
            return {};
        }

        constexpr uint32_t kOutputSrgb =
            (kColorFormat == rhi::PixelFormat::eRGBA8_sRGB || kColorFormat == rhi::PixelFormat::eBGRA8_sRGB) ? 1u : 0u;
        auto fragmentShaderVariantHash = getShaderLib().computeVariantHash(
            "gaussian_splat.frag", vshadersystem::ShaderStage::eFrag, {{"SPLAT_OUTPUT_SRGB", kOutputSrgb}});
        auto fragmentShader = getShaderLib().load(fragmentShaderVariantHash, vshadersystem::ShaderStage::eFrag);
        if (!fragmentShader)
        {
            VULTRA_CORE_ERROR("[GaussianSplatRenderPass] Failed to load fragment shader variant");
            return {};
        }

        return rhi::GraphicsPipeline::Builder {}
            .setColorFormats({kColorFormat})
            .setDepthFormat(rhi::PixelFormat::eDepth32F)
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
            .setBlending(0,
                         {
                             .enabled  = true,
                             .srcColor = rhi::BlendFactor::eOne,
                             .dstColor = rhi::BlendFactor::eOneMinusSrcAlpha,
                             .colorOp  = rhi::BlendOp::eAdd,
                             .srcAlpha = rhi::BlendFactor::eOne,
                             .dstAlpha = rhi::BlendFactor::eOneMinusSrcAlpha,
                             .alphaOp  = rhi::BlendOp::eAdd,
                         })
            .build(getRenderDevice());
    }
} // namespace vultra
