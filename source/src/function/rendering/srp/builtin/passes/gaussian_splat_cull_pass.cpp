#include "vultra/function/rendering/srp/builtin/passes/gaussian_splat_cull_pass.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/function/framegraph/framegraph_buffer.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"
#include "vultra/function/rendering/srp/builtin/resource_keys.hpp"

#include <fg/FrameGraph.hpp>

namespace vultra
{
    namespace
    {
        constexpr auto PASS_NAME = "GaussianSplatCullPass";

        struct SortKeysPushConstants
        {
            uint32_t totalPointCount {0};
            uint32_t maxOutputCount {0};
            float    frustumDilation {1.10f};
            float    alphaCullThreshold {1.0f / 255.0f};
            float    sizeCullingMinPixels {0.25f};
            float    splatScale {1.0f};
            float    maxAxisPixels {2048.0f};
        };
    } // namespace

    FrameGraphResource GaussianSplatCullPass::addPass(FrameGraphBuildContext&              ctx,
                                                      FrameGraphResource                   buildToken,
                                                      const GaussianSplatRendererSettings& settings)
    {
        struct PassData
        {
            FrameGraphResource            camera;
            FrameGraphResource            buildToken;
            FrameGraphResource            token;
            FrameGraphResource            depth;
            GaussianSplatRendererSettings settings;
        };

        const auto cameraBlock = ctx.bb.get<CameraData>().cameraBlock.fgResource;
        const auto depthPre    = ctx.data.tryGet(kResKey_DepthTexture);

        auto data = ctx.fg.addCallbackPass<PassData>(
            PASS_NAME,
            [cameraBlock, buildToken, settings, depthPre](FrameGraph::Builder& builder, PassData& pd) {
                PASS_SETUP_ZONE;
                pd.settings = settings;

                pd.camera = builder.read(cameraBlock,
                                         framegraph::BindingInfo {
                                             .location      = {.set = 0, .binding = 0},
                                             .pipelineStage = framegraph::PipelineStage::eComputeShader,
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

                pd.depth = depthPre;
                if (pd.depth)
                {
                    pd.depth = builder.read(pd.depth,
                                            framegraph::TextureRead {
                                                .binding =
                                                    {
                                                        .location      = {.set = 0, .binding = 27},
                                                        .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                                    },
                                                .type        = framegraph::TextureRead::Type::eCombinedImageSampler,
                                                .imageAspect = rhi::ImageAspect::eDepth,
                                            });
                }

                pd.token =
                    builder.create<framegraph::FrameGraphBuffer>("GaussianSplatCullToken",
                                                                 {
                                                                     .type     = framegraph::BufferType::eStorageBuffer,
                                                                     .stride   = sizeof(uint32_t),
                                                                     .capacity = 1,
                                                                 });
                pd.token = builder.write(pd.token,
                                         framegraph::BindingInfo {
                                             .location      = {},
                                             .pipelineStage = framegraph::PipelineStage::eTransfer,
                                         });
            },
            [this](const PassData& pd, FrameGraphPassResources& resources, void* ctxPtr) {
                auto& rc = *static_cast<FrameGraphExecContext*>(ctxPtr);
                setRenderDevice(rc.rd);
                if (!rc.ext.builtinShaderLib)
                    return;
                setShaderLib(*rc.ext.builtinShaderLib);

                RHI_GPU_ZONE(rc.cb, PASS_NAME);

                auto* gpuSceneView     = rc.view().gpuSceneView;
                auto* gpuSceneDatabase = rc.view().gpuSceneDatabase;
                if (gpuSceneView)
                {
                    gpuSceneView->ensureGaussianSplatDrawBuffer(rc.rd);
                    gpuSceneView->ensureGaussianSplatVisibleCountBuffer(rc.rd);
                    gpuSceneView->ensureGaussianSplatIndirectBuffer(rc.rd);
                }
                if (!gpuSceneView || !gpuSceneView->gaussianSplatDrawBuffer ||
                    !gpuSceneView->gaussianSplatVisibleCountBuffer ||
                    !gpuSceneView->gaussianSplatIndirectBuffer.has_value() ||
                    !gpuSceneView->gaussianSplatPointDrawIdBuffer)
                    return;

                if (!gpuSceneDatabase || !gpuSceneDatabase->resources)
                    return;

                const auto& splatStorage = gpuSceneDatabase->resources->gaussianStorage;
                if (!splatStorage.centersBuffer || !splatStorage.scaleBuffer || !splatStorage.colorBuffer ||
                    !gpuSceneDatabase->resources->gaussianSplatMetaBuffer)
                    return;

                auto* cameraUbo = resources.get<framegraph::FrameGraphBuffer>(pd.camera).buffer;
                if (!cameraUbo)
                    return;

                const uint32_t drawCount = gpuSceneView->getDispatchableGaussianSplatDrawCount();
                if (drawCount == 0u)
                    return;

                uint32_t totalPointCount = 0u;
                for (uint32_t drawId = 0; drawId < drawCount; ++drawId)
                {
                    if (drawId >= gpuSceneView->gaussianSplatDraws.size())
                        continue;
                    const auto& draw = gpuSceneView->gaussianSplatDraws[drawId];
                    if (draw.primitiveIndex >= gpuSceneDatabase->resources->gaussianSplats.size())
                        continue;
                    totalPointCount += gpuSceneDatabase->resources->gaussianSplats[draw.primitiveIndex].pointCount;
                }
                if (totalPointCount == 0u)
                    return;

                rc.cb.clear(*gpuSceneView->gaussianSplatVisibleCountBuffer, 0u);

                rhi::prepareForComputing(rc.cb, *gpuSceneView->gaussianSplatDrawBuffer);
                rhi::prepareForComputing(rc.cb, *gpuSceneView->gaussianSplatVisibleCountBuffer);
                rhi::prepareForComputing(rc.cb, gpuSceneView->gaussianSplatIndirectBuffer.value());
                rhi::prepareForComputing(rc.cb, *gpuSceneView->gaussianSplatPointDrawIdBuffer);
                rhi::prepareForComputing(rc.cb, *splatStorage.centersBuffer);
                rhi::prepareForComputing(rc.cb, *splatStorage.scaleBuffer);
                rhi::prepareForComputing(rc.cb, *splatStorage.colorBuffer);
                rhi::prepareForComputing(rc.cb, *gpuSceneDatabase->resources->gaussianSplatMetaBuffer);

                const uint32_t useSceneDepth   = pd.depth ? 1u : 0u;
                auto           sortVariantHash = getShaderLib().computeVariantHash("gaussian_splat_sort_keys.comp",
                                                                         vshadersystem::ShaderStage::eComp,
                                                                                   {{"USE_SCENE_DEPTH", useSceneDepth}});
                const auto*    sortPipeline    = getPipeline(sortVariantHash);
                if (!sortPipeline)
                    return;

                if (!m_RadixSorter.has_value() || m_RadixSorterMaxElementCount < totalPointCount)
                {
                    m_RadixSorter                = rc.rd.createRadixSorter(totalPointCount);
                    m_RadixSorterMaxElementCount = totalPointCount;
                }
                if (!m_RadixSorter.has_value() || !m_RadixSorter.value())
                    return;

                gpuSceneView->ensureGaussianSplatSortBuffers(rc.rd, m_RadixSorter.value(), totalPointCount);
                if (!gpuSceneView->gaussianSplatSortKeysBuffer || !gpuSceneView->gaussianSplatSortValuesBuffer ||
                    !gpuSceneView->gaussianSplatSortStorageBuffer)
                    return;

                rhi::prepareForComputing(rc.cb, *gpuSceneView->gaussianSplatSortKeysBuffer);
                rhi::prepareForComputing(rc.cb, *gpuSceneView->gaussianSplatSortValuesBuffer);
                rhi::prepareForComputing(rc.cb, *gpuSceneView->gaussianSplatSortStorageBuffer);

                rc.resourceSet[0] = {
                    {0, rhi::bindings::UniformBuffer {.buffer = cameraUbo}},
                    {1, rhi::bindings::StorageBuffer {.buffer = gpuSceneView->gaussianSplatDrawBuffer.get()}},
                    {13, rhi::bindings::StorageBuffer {.buffer = splatStorage.centersBuffer.get()}},
                    {15, rhi::bindings::StorageBuffer {.buffer = splatStorage.colorBuffer.get()}},
                    {19,
                     rhi::bindings::StorageBuffer {.buffer =
                                                       gpuSceneDatabase->resources->gaussianSplatMetaBuffer.get()}},
                    {17, rhi::bindings::StorageBuffer {.buffer = gpuSceneView->gaussianSplatSortKeysBuffer.get()}},
                    {18, rhi::bindings::StorageBuffer {.buffer = gpuSceneView->gaussianSplatSortValuesBuffer.get()}},
                    {20, rhi::bindings::StorageBuffer {.buffer = gpuSceneView->gaussianSplatVisibleCountBuffer.get()}},
                    {21, rhi::bindings::StorageBuffer {.buffer = gpuSceneView->gaussianSplatPointDrawIdBuffer.get()}},
                    {23, rhi::bindings::StorageBuffer {.buffer = splatStorage.scaleBuffer.get()}},
                };
                if (pd.depth)
                {
                    if (auto* depthTexture = resources.get<framegraph::FrameGraphTexture>(pd.depth).texture;
                        depthTexture)
                    {
                        rc.resourceSet[0][27] = rhi::bindings::CombinedImageSampler {
                            .texture     = depthTexture,
                            .imageAspect = rhi::ImageAspect::eDepth,
                        };
                    }
                }

                SortKeysPushConstants pc {};
                pc.totalPointCount      = totalPointCount;
                pc.maxOutputCount       = totalPointCount;
                pc.frustumDilation      = pd.settings.frustumDilation;
                pc.alphaCullThreshold   = pd.settings.alphaCullThreshold;
                pc.sizeCullingMinPixels = pd.settings.sizeCullingMinPixels;
                pc.splatScale           = pd.settings.splatScale;
                pc.maxAxisPixels        = pd.settings.maxAxisPixels;

                {
                    RHI_GPU_ZONE(rc.cb, "GaussianSplatCullPass::Dist");
                    rc.cb.bindPipeline(*sortPipeline);
                    rc.bindDescriptorSets(*sortPipeline);
                    rc.cb.pushConstants(rhi::ShaderStages::eCompute, 0, &pc);
                    rc.cb.dispatch({(totalPointCount + 63u) / 64u, 1u, 1u});
                }
                rc.cb.insertComputeUavBarrier();

                if (totalPointCount > 1u)
                {
                    RHI_GPU_ZONE(rc.cb, "GaussianSplatCullPass::RadixSortIndirect");
                    m_RadixSorter->sortKeyValuesIndirect(rc.cb,
                                                         totalPointCount,
                                                         *gpuSceneView->gaussianSplatVisibleCountBuffer,
                                                         0,
                                                         *gpuSceneView->gaussianSplatSortKeysBuffer,
                                                         0,
                                                         *gpuSceneView->gaussianSplatSortValuesBuffer,
                                                         0,
                                                         *gpuSceneView->gaussianSplatSortStorageBuffer,
                                                         0);
                    rc.cb.insertComputeUavBarrier();
                }

                auto writeIndirectVariantHash = getShaderLib().computeVariantHash(
                    "gaussian_splat_write_indirect.comp", vshadersystem::ShaderStage::eComp, {});
                const auto* writeIndirectPipeline = getPipeline(writeIndirectVariantHash);
                if (writeIndirectPipeline)
                {
                    rc.resourceSet[0] = {
                        {12,
                         rhi::bindings::StorageBuffer {.buffer = &gpuSceneView->gaussianSplatIndirectBuffer.value()}},
                        {20,
                         rhi::bindings::StorageBuffer {.buffer = gpuSceneView->gaussianSplatVisibleCountBuffer.get()}},
                    };

                    RHI_GPU_ZONE(rc.cb, "GaussianSplatCullPass::WriteIndirect");
                    rc.cb.bindPipeline(*writeIndirectPipeline);
                    rc.bindDescriptorSets(*writeIndirectPipeline);
                    rc.cb.dispatch({1u, 1u, 1u});
                    rc.cb.insertComputeUavBarrier();
                }
                rc.clear();
            });

        return data.token;
    }

    rhi::ComputePipeline GaussianSplatCullPass::createPipeline(uint64_t variantHash) const
    {
        auto shader = getShaderLib().load(variantHash, vshadersystem::ShaderStage::eComp);
        if (!shader)
        {
            VULTRA_CORE_ERROR("[GaussianSplatCullPass] Failed to load compute shader variant");
            return {};
        }
        return getRenderDevice().createComputePipelineBuiltin(shader->spirv);
    }
} // namespace vultra
