#include "vultra/function/rendering/srp/builtin/passes/compatibility_gaussian_splat_cull_pass.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/structs/render_backend_api.hpp"
#include "vultra/function/framegraph/framegraph_buffer.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"

#include <fg/FrameGraph.hpp>

#include <algorithm>

namespace vultra
{
    CompatibilityGaussianSplatCullPass::CompatibilityGaussianSplatCullPass()
    {
        setShaderProfile(rhi::ShaderProfile::eCompatibility);
    }

    namespace
    {
        constexpr auto PASS_NAME = "CompatibilityGaussianSplatCullPass";
        constexpr uint32_t kCullThreads = 64u;

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

    FrameGraphResource CompatibilityGaussianSplatCullPass::addPass(FrameGraphBuildContext&              ctx,
                                                                   FrameGraphResource                   buildToken,
                                                                   const GaussianSplatRendererSettings& settings)
    {
        struct PassData
        {
            FrameGraphResource camera;
            FrameGraphResource token;
        };

        const auto                  cameraBlock = ctx.bb.get<CameraData>().cameraBlock.fgResource;
        const SortKeysPushConstants basePushConstants {
            .totalPointCount      = 0u,
            .maxOutputCount       = 0u,
            .frustumDilation      = settings.frustumDilation,
            .alphaCullThreshold   = settings.alphaCullThreshold,
            .sizeCullingMinPixels = settings.sizeCullingMinPixels,
            .splatScale           = settings.splatScale,
            .maxAxisPixels        = settings.maxAxisPixels,
        };

        auto data = ctx.fg.addCallbackPass<PassData>(
            PASS_NAME,
            [cameraBlock, buildToken](FrameGraph::Builder& builder, PassData& pd) {
                PASS_SETUP_ZONE;

                pd.camera = builder.read(cameraBlock,
                                         framegraph::BindingInfo {
                                             .location      = {.set = 0, .binding = 0},
                                             .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                         });

                if (buildToken)
                {
                    builder.read(buildToken,
                                 framegraph::BindingInfo {
                                     .location      = {},
                                     .pipelineStage = framegraph::PipelineStage::eTransfer,
                                 });
                }

                pd.token =
                    builder.create<framegraph::FrameGraphBuffer>("CompatibilityGaussianSplatCullToken",
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
            [this, basePushConstants](const PassData& pd, FrameGraphPassResources& resources, void* ctxPtr) {
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
                {
                    return;
                }

                if (!gpuSceneDatabase || !gpuSceneDatabase->resources)
                    return;

                const auto& splatStorage = gpuSceneDatabase->resources->gaussianStorage;
                if (!splatStorage.centersBuffer || !splatStorage.colorBuffer ||
                    !gpuSceneDatabase->resources->gaussianSplatMetaBuffer)
                {
                    return;
                }

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

                const auto limits = rc.rd.getLimits();
                const uint32_t maxWorkgroups =
                    limits.maxComputeWorkgroupsPerDimension > 0u ? limits.maxComputeWorkgroupsPerDimension : 65535u;
                const uint64_t maxPointCountByDispatch = static_cast<uint64_t>(maxWorkgroups) * kCullThreads;
                const uint64_t maxPointCountByPointIdBuffer = gpuSceneView->gaussianSplatPointDrawIdBuffer ?
                                                                   static_cast<uint64_t>(
                                                                       gpuSceneView->gaussianSplatPointDrawIdBuffer->getSize()) /
                                                                       sizeof(uint32_t) :
                                                                   0u;
                uint32_t effectivePointCount                = static_cast<uint32_t>(std::min<uint64_t>(
                    totalPointCount,
                    std::min<uint64_t>(maxPointCountByDispatch, maxPointCountByPointIdBuffer)));
                if (effectivePointCount == 0u)
                    return;
                if (effectivePointCount < totalPointCount && !m_HasLoggedDispatchClamp)
                {
                    VULTRA_CORE_WARN(
                        "[CompatibilityGaussianSplatCullPass] Clamping total point count {} -> {} due to compute workgroup limit {}",
                        totalPointCount,
                        effectivePointCount,
                        maxWorkgroups);
                    m_HasLoggedDispatchClamp = true;
                }

                rc.cb.clear(*gpuSceneView->gaussianSplatVisibleCountBuffer, 0u);

                // Initialize radix sorter if needed
                if (!m_RadixSorter.has_value() || m_RadixSorterMaxElementCount < effectivePointCount)
                {
                    m_RadixSorter                = rc.rd.createRadixSorter(effectivePointCount);
                    m_RadixSorterMaxElementCount = effectivePointCount;
                }
                if (!m_RadixSorter.has_value() || !m_RadixSorter.value())
                    return;

                const auto memoryLimits = rc.rd.getLimits();
                const uint64_t maxStorageBytes =
                    memoryLimits.maxStorageBufferBindingSize > 0u ? memoryLimits.maxStorageBufferBindingSize : UINT64_MAX;
                const uint64_t maxBufferBytes = memoryLimits.maxBufferSize > 0u ? memoryLimits.maxBufferSize : UINT64_MAX;
                const uint64_t maxBytes       = std::min(maxStorageBytes, maxBufferBytes);

                if (maxBytes != UINT64_MAX)
                {
                    const auto storageReq = m_RadixSorter.value().getKeyValueStorageRequirements();
                    if (storageReq.size > maxBytes)
                    {
                        if (!m_HasLoggedMemoryClamp)
                        {
                            VULTRA_CORE_WARN(
                                "[CompatibilityGaussianSplatCullPass] Skipping sort buffer allocation because required bytes {} exceed device limit {}",
                                storageReq.size,
                                maxBytes);
                            m_HasLoggedMemoryClamp = true;
                        }
                        return;
                    }
                }

                // Ensure sort buffers are allocated before clearing
                gpuSceneView->ensureGaussianSplatSortBuffers(rc.rd, m_RadixSorter.value(), effectivePointCount);
                const uint32_t sortCapacity = gpuSceneView->maxGaussianSplatSortElements;
                if (sortCapacity == 0u)
                    return;
                if (effectivePointCount > sortCapacity)
                {
                    if (!m_HasLoggedMemoryClamp)
                    {
                        VULTRA_CORE_WARN(
                            "[CompatibilityGaussianSplatCullPass] Clamping point count {} -> {} due to sort buffer memory limits",
                            effectivePointCount,
                            sortCapacity);
                        m_HasLoggedMemoryClamp = true;
                    }
                    effectivePointCount = sortCapacity;
                }

                // Clear sort buffers to prevent stale data from previous frame
                rc.cb.clear(*gpuSceneView->gaussianSplatSortKeysBuffer, 0u);
                rc.cb.clear(*gpuSceneView->gaussianSplatSortValuesBuffer, 0u);
                rc.cb.clear(*gpuSceneView->gaussianSplatSortStorageBuffer, 0u);

                rhi::prepareForComputing(rc.cb, *gpuSceneView->gaussianSplatDrawBuffer);
                rhi::prepareForComputing(rc.cb, *gpuSceneView->gaussianSplatVisibleCountBuffer);
                rhi::prepareForComputing(rc.cb, gpuSceneView->gaussianSplatIndirectBuffer.value());
                rhi::prepareForComputing(rc.cb, *gpuSceneView->gaussianSplatPointDrawIdBuffer);
                rhi::prepareForComputing(rc.cb, *splatStorage.centersBuffer);
                rhi::prepareForComputing(rc.cb, *splatStorage.colorBuffer);
                rhi::prepareForComputing(rc.cb, *gpuSceneDatabase->resources->gaussianSplatMetaBuffer);

                auto sortVariantHash = computeCompatibilityVariantHash(
                    "gaussian_splat_compat_sort_keys.comp", vshadersystem::ShaderStage::eComp, {});
                const auto* sortPipeline = getPipeline(sortVariantHash);
                if (!sortPipeline)
                    return;

                // Sort buffers already ensured and cleared at beginning of pass
                if (!gpuSceneView->gaussianSplatSortKeysBuffer || !gpuSceneView->gaussianSplatSortValuesBuffer ||
                    !gpuSceneView->gaussianSplatSortStorageBuffer)
                {
                    return;
                }

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
                };

                SortKeysPushConstants pc = basePushConstants;
                pc.totalPointCount       = effectivePointCount;
                pc.maxOutputCount        = effectivePointCount;

                {
                    RHI_GPU_ZONE(rc.cb, "CompatibilityGaussianSplatCullPass::Dist");
                    rc.cb.bindPipeline(*sortPipeline);
                    rc.bindDescriptorSets(*sortPipeline);
                    rc.cb.pushConstants(rhi::ShaderStages::eCompute, 0, &pc);
                    rc.cb.dispatch({(effectivePointCount + kCullThreads - 1u) / kCullThreads, 1u, 1u});
                }
                rc.cb.insertComputeUavBarrier();

                if (effectivePointCount > 1u)
                {
                    RHI_GPU_ZONE(rc.cb, "CompatibilityGaussianSplatCullPass::Sort");
                    m_RadixSorter->sortKeyValuesIndirect(rc.cb,
                                                         effectivePointCount,
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

                auto writeIndirectVariantHash = computeCompatibilityVariantHash(
                    "gaussian_splat_compat_write_indirect.comp", vshadersystem::ShaderStage::eComp, {});
                const auto* writeIndirectPipeline = getPipeline(writeIndirectVariantHash);
                if (writeIndirectPipeline)
                {
                    rc.resourceSet[0] = {
                        {12,
                         rhi::bindings::StorageBuffer {.buffer = &gpuSceneView->gaussianSplatIndirectBuffer.value()}},
                        {20,
                         rhi::bindings::StorageBuffer {.buffer = gpuSceneView->gaussianSplatVisibleCountBuffer.get()}},
                    };

                    RHI_GPU_ZONE(rc.cb, "CompatibilityGaussianSplatCullPass::WriteIndirect");
                    rc.cb.bindPipeline(*writeIndirectPipeline);
                    rc.bindDescriptorSets(*writeIndirectPipeline);
                    rc.cb.dispatch({1u, 1u, 1u});
                    rc.cb.insertComputeUavBarrier();
                }

                rc.clear();
            });

        return data.token;
    }

    rhi::ComputePipeline CompatibilityGaussianSplatCullPass::createPipeline(const uint64_t variantHash) const
    {
        auto shader = loadCompatibilityShaderVariant(variantHash, vshadersystem::ShaderStage::eComp);
        if (!shader)
        {
            VULTRA_CORE_ERROR("[CompatibilityGaussianSplatCullPass] Failed to load compute shader variant");
            return {};
        }

        if (getRenderDevice().getBackendApi() == rhi::RenderBackendApi::eWebGPU)
        {
            return getRenderDevice().createComputePipeline(
                {.code = shader->wgsl, .reflection = shader->reflection},
                {});
        }

        return getRenderDevice().createComputePipelineBuiltin(shader->spirv);
    }
} // namespace vultra
