#include "vultra/function/rendering/srp/builtin/passes/compatibility_gaussian_splat_render_pass.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/structs/render_backend_api.hpp"
#include "vultra/function/framegraph/framegraph_buffer.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"
#include "vultra/function/rendering/srp/builtin/resource_keys.hpp"

#include <fg/FrameGraph.hpp>

namespace vultra
{
    CompatibilityGaussianSplatRenderPass::CompatibilityGaussianSplatRenderPass()
    {
        setShaderProfile(rhi::ShaderProfile::eCompatibility);
    }

    namespace
    {
        constexpr auto PASS_NAME = "CompatibilityGaussianSplatRenderPass";

        struct RasterPushConstants
        {
            float frustumDilation {1.10f};
            float alphaCullThreshold {1.0f / 255.0f};
            float sizeCullingMinPixels {0.25f};
            float splatScale {1.0f};
            float maxAxisPixels {2048.0f};
            float depthIsoThreshold {0.7f};
        };

        [[nodiscard]] uint32_t countTotalPoints(const resource::GpuSceneView&     gpuSceneView,
                                                const resource::GpuSceneDatabase& gpuSceneDatabase)
        {
            if (!gpuSceneDatabase.resources)
                return 0u;

            uint32_t totalPointCount = 0u;
            const auto drawCount     = gpuSceneView.getDispatchableGaussianSplatDrawCount();
            for (uint32_t drawId = 0; drawId < drawCount; ++drawId)
            {
                if (drawId >= gpuSceneView.gaussianSplatDraws.size())
                    continue;
                const auto& draw = gpuSceneView.gaussianSplatDraws[drawId];
                if (draw.primitiveIndex >= gpuSceneDatabase.resources->gaussianSplats.size())
                    continue;
                totalPointCount += gpuSceneDatabase.resources->gaussianSplats[draw.primitiveIndex].pointCount;
            }
            return totalPointCount;
        }
    } // namespace

    FrameGraphResource CompatibilityGaussianSplatRenderPass::addPass(FrameGraphBuildContext&              ctx,
                                                                     FrameGraphResource                   buildToken,
                                                                     FrameGraphResource                   target,
                                                                     const GaussianSplatRendererSettings& settings)
    {
        struct PassData
        {
            FrameGraphResource camera;
            FrameGraphResource color;
        };

        const auto             resolution        = ctx.view().extent;
        const auto             cameraBlock       = ctx.bb.get<CameraData>().cameraBlock.fgResource;
        const RasterPushConstants pushConstants {
            .frustumDilation      = settings.frustumDilation,
            .alphaCullThreshold   = settings.alphaCullThreshold,
            .sizeCullingMinPixels = settings.sizeCullingMinPixels,
            .splatScale           = settings.splatScale,
            .maxAxisPixels        = settings.maxAxisPixels,
            .depthIsoThreshold    = settings.depthIsoThreshold,
        };

        auto data = ctx.fg.addCallbackPass<PassData>(
            PASS_NAME,
            [cameraBlock, buildToken, target, resolution](FrameGraph::Builder& builder, PassData& pd) {
                PASS_SETUP_ZONE;

                pd.camera = builder.read(cameraBlock,
                                         framegraph::BindingInfo {
                                             .location      = {.set = 0, .binding = 0},
                                             .pipelineStage = framegraph::PipelineStage::eVertexShader,
                                         });

                if (buildToken)
                {
                    builder.read(buildToken,
                                 framegraph::BindingInfo {
                                     .location      = {},
                                     .pipelineStage = framegraph::PipelineStage::eTransfer,
                                 });
                }

                if (target)
                {
                    pd.color = builder.write(target,
                                             framegraph::Attachment {
                                                 .index       = 0,
                                                 .imageAspect = rhi::ImageAspect::eColor,
                                             });
                }
                else
                {
                    pd.color = builder.create<framegraph::FrameGraphTexture>(
                        "CompatibilityGaussianSplatColor",
                        {
                            .extent     = resolution,
                            .format     = rhi::PixelFormat::eRGBA16F,
                            .usageFlags = rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled,
                        });
                    pd.color = builder.write(pd.color,
                                             framegraph::Attachment {
                                                 .index       = 0,
                                                 .imageAspect = rhi::ImageAspect::eColor,
                                                 .clearValue  = framegraph::ClearValue::eTransparentBlack,
                                             });
                }
            },
            [this, pushConstants](const PassData& pd, FrameGraphPassResources& resources, void* ctxPtr) {
                auto& rc = *static_cast<FrameGraphExecContext*>(ctxPtr);
                setRenderDevice(rc.rd);
                if (!rc.ext.builtinShaderLib)
                    return;
                setShaderLib(*rc.ext.builtinShaderLib);

                RHI_GPU_ZONE(rc.cb, PASS_NAME);

                auto* gpuSceneView     = rc.view().gpuSceneView;
                auto* gpuSceneDatabase = rc.view().gpuSceneDatabase;
                auto* cameraUbo        = resources.get<framegraph::FrameGraphBuffer>(pd.camera).buffer;
                auto* colorTexture     = resources.get<framegraph::FrameGraphTexture>(pd.color).texture;
                const uint32_t totalPointCount =
                    (gpuSceneView && gpuSceneDatabase) ? countTotalPoints(*gpuSceneView, *gpuSceneDatabase) : 0u;

                const bool canDraw = cameraUbo && colorTexture && gpuSceneView && gpuSceneDatabase &&
                                     gpuSceneDatabase->resources && gpuSceneView->gaussianSplatDrawBuffer &&
                                     gpuSceneView->gaussianSplatPointDrawIdBuffer &&
                                     totalPointCount > 0u;
                if (!canDraw)
                    return;

                const bool webgpu = rc.rd.getBackendApi() == rhi::RenderBackendApi::eWebGPU;
                const bool useSortedIds = gpuSceneView->gaussianSplatSortValuesBuffer &&
                                          gpuSceneView->gaussianSplatIndirectBuffer.has_value() &&
                                          gpuSceneView->maxGaussianSplatSortElements > 0u;
                const bool useIndirectDraw = useSortedIds;
                const auto* pipeline = getPipeline(colorTexture->getPixelFormat(), useSortedIds);
                if (!pipeline)
                    return;

                const auto& splatStorage = gpuSceneDatabase->resources->gaussianStorage;
                if (!splatStorage.centersBuffer || !splatStorage.covarianceBuffer || !splatStorage.colorBuffer ||
                    !splatStorage.shBuffer || !gpuSceneDatabase->resources->gaussianSplatMetaBuffer)
                {
                    return;
                }

                rhi::prepareForReading(rc.cb, *gpuSceneView->gaussianSplatDrawBuffer);
                rhi::prepareForReading(rc.cb, *gpuSceneView->gaussianSplatPointDrawIdBuffer);
                rhi::prepareForReading(rc.cb, *splatStorage.centersBuffer);
                rhi::prepareForReading(rc.cb, *splatStorage.covarianceBuffer);
                rhi::prepareForReading(rc.cb, *splatStorage.colorBuffer);
                rhi::prepareForReading(rc.cb, *splatStorage.shBuffer);
                rhi::prepareForReading(rc.cb, *gpuSceneDatabase->resources->gaussianSplatMetaBuffer);
                if (useSortedIds)
                    rhi::prepareForReading(rc.cb, *gpuSceneView->gaussianSplatSortValuesBuffer);
                if (useIndirectDraw)
                    rhi::prepareForReading(rc.cb, gpuSceneView->gaussianSplatIndirectBuffer.value());

                assert(rc.framebufferInfo().has_value());
                auto framebufferInfo = rc.framebufferInfo().value();
                framebufferInfo.depthAttachment   = std::nullopt;
                framebufferInfo.stencilAttachment = std::nullopt;
                framebufferInfo.depthReadOnly     = false;
                framebufferInfo.stencilReadOnly   = false;
                rc.cb.beginRendering(framebufferInfo);

                rc.cb.bindPipeline(*pipeline);
                rc.resourceSet[0] = {
                    {0, rhi::bindings::UniformBuffer {.buffer = cameraUbo}},
                    {1, rhi::bindings::StorageBuffer {.buffer = gpuSceneView->gaussianSplatDrawBuffer.get()}},
                    {13, rhi::bindings::StorageBuffer {.buffer = splatStorage.centersBuffer.get()}},
                    {14, rhi::bindings::StorageBuffer {.buffer = splatStorage.covarianceBuffer.get()}},
                    {15, rhi::bindings::StorageBuffer {.buffer = splatStorage.colorBuffer.get()}},
                    {16, rhi::bindings::StorageBuffer {.buffer = splatStorage.shBuffer.get()}},
                    {19,
                     rhi::bindings::StorageBuffer {
                         .buffer = gpuSceneDatabase->resources->gaussianSplatMetaBuffer.get()}},
                    {21, rhi::bindings::StorageBuffer {.buffer = gpuSceneView->gaussianSplatPointDrawIdBuffer.get()}},
                };
                if (useSortedIds)
                    rc.resourceSet[0][18] =
                        rhi::bindings::StorageBuffer {.buffer = gpuSceneView->gaussianSplatSortValuesBuffer.get()};
                rc.bindDescriptorSets(*pipeline);
                rc.cb.pushConstants(rhi::ShaderStages::eVertex | rhi::ShaderStages::eFragment, 0, &pushConstants);
                if (useIndirectDraw)
                {
                    if (webgpu)
                    {
                        static bool s_LoggedWebGPUPath = false;
                        if (!s_LoggedWebGPUPath)
                        {
                            VULTRA_CORE_INFO("[CompatibilityGaussianSplatRenderPass] WebGPU path active "
                                             "(sorted ids + indirect draw, totalPointCount={})",
                                             totalPointCount);
                            s_LoggedWebGPUPath = true;
                        }
                    }
                    rc.cb.drawIndirect(rhi::DrawIndirectInfo {
                        .buffer       = &gpuSceneView->gaussianSplatIndirectBuffer.value(),
                        .firstCommand = 0,
                        .commandCount = 1,
                    });
                }
                else
                {
                    rc.cb.draw(rhi::GeometryInfo {
                                   .topology    = rhi::PrimitiveTopology::eTriangleStrip,
                                   .numVertices = 4,
                               },
                               totalPointCount);
                }

                rc.cb.endRendering();
                rc.clear();
            });

        return data.color;
    }

    rhi::GraphicsPipeline CompatibilityGaussianSplatRenderPass::createPipeline(const rhi::PixelFormat colorFormat,
                                                                                const bool useSortedIds) const
    {
        auto vertexShader = loadCompatibilityShader("gaussian_splat_compat.vert",
                                                    vshadersystem::ShaderStage::eVert,
                                                    {{"USE_SORTED_IDS", useSortedIds ? 1u : 0u}});
        if (!vertexShader)
        {
            VULTRA_CORE_ERROR("[CompatibilityGaussianSplatRenderPass] Failed to load vertex shader");
            return {};
        }

        auto fragmentShader = loadCompatibilityShader("gaussian_splat_compat.frag", vshadersystem::ShaderStage::eFrag);
        if (!fragmentShader)
        {
            VULTRA_CORE_ERROR("[CompatibilityGaussianSplatRenderPass] Failed to load fragment shader");
            return {};
        }

        rhi::GraphicsPipeline::Builder builder {};
        builder.setColorFormats({colorFormat})
            .setInputAssembly({})
            .setTopology(rhi::PrimitiveTopology::eTriangleStrip)
            .setDepthStencil({
                .depthTest      = false,
                .depthWrite     = false,
                .depthCompareOp = rhi::CompareOp::eLessOrEqual,
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
                         });

        if (getRenderDevice().getBackendApi() == rhi::RenderBackendApi::eWebGPU)
        {
            return builder
                .addShader(rhi::ShaderType::eVertex, {.code = vertexShader->wgsl, .reflection = vertexShader->reflection})
                .addShader(rhi::ShaderType::eFragment,
                           {.code = fragmentShader->wgsl, .reflection = fragmentShader->reflection})
                .build(getRenderDevice());
        }

        return builder.addBuiltinShader(rhi::ShaderType::eVertex, vertexShader->spirv)
            .addBuiltinShader(rhi::ShaderType::eFragment, fragmentShader->spirv)
            .build(getRenderDevice());
    }
} // namespace vultra
