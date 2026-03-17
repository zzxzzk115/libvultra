#include "vultra/function/rendering/srp/builtin/passes/gaussian_splat_cull_pass.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/function/framegraph/framegraph_buffer.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"

#include <fg/FrameGraph.hpp>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <array>
#include <algorithm>
#include <limits>
#include <unordered_map>
#include <vector>

namespace vultra
{
    namespace
    {
        constexpr auto PASS_NAME = "GaussianSplatCullPass";

        [[nodiscard]] bool isSrgbPixelFormat(rhi::PixelFormat pf)
        {
            return pf == rhi::PixelFormat::eRGBA8_sRGB || pf == rhi::PixelFormat::eBGRA8_sRGB;
        }

        struct SortKeysPushConstants
        {
            uint32_t drawId {0};
            uint32_t pointCount {0};
            uint32_t valueBase {0};
            uint32_t shDegree {0};
        };

        [[nodiscard]] float extractMaxScale(const glm::mat4& model)
        {
            const glm::vec3 x = glm::vec3(model[0]);
            const glm::vec3 y = glm::vec3(model[1]);
            const glm::vec3 z = glm::vec3(model[2]);
            return glm::max(glm::length(x), glm::max(glm::length(y), glm::length(z)));
        }

        [[nodiscard]] bool definitelyOutsideTileExpandedView(const RenderCamera&             camera,
                                                             const rhi::Extent2D             extent,
                                                             const resource::GpuGaussianSplat& splat,
                                                             const glm::mat4&                model)
        {
            const glm::vec4 worldCenter = model * glm::vec4(splat.center, 1.0f);
            const glm::vec4 viewCenter4 = camera.view * worldCenter;
            const glm::vec3 viewCenter  = glm::vec3(viewCenter4);

            if (viewCenter.z >= -0.02f)
                return true;

            const float depth = glm::max(-viewCenter.z, 1e-4f);
            const float ndcX = (camera.projection[0][0] * viewCenter.x) / depth;
            const float ndcY = (camera.projection[1][1] * viewCenter.y) / depth;

            const float extentX = static_cast<float>(extent.width);
            const float extentY = static_cast<float>(extent.height);
            if (extentX <= 1.0f || extentY <= 1.0f)
                return false;

            constexpr float kExtentStdDev = 2.8284271247461903f;
            constexpr float kTileSizePx = 16.0f;

            const float maxScale = extractMaxScale(model);
            const float pxPerWorldX = 0.5f * extentX * std::abs(camera.projection[0][0]) / depth;
            const float pxPerWorldY = 0.5f * extentY * std::abs(camera.projection[1][1]) / depth;
            const float pxPerWorld  = glm::max(pxPerWorldX, pxPerWorldY);

            const float axisPxUpper = kExtentStdDev * glm::max(splat.radius * maxScale, 1e-4f) * pxPerWorld;
            const float padPx       = axisPxUpper + (2.0f * kTileSizePx);

            const float padNdcX = (2.0f * padPx) / extentX;
            const float padNdcY = (2.0f * padPx) / extentY;

            return ndcX < (-1.0f - padNdcX) || ndcX > (1.0f + padNdcX) || ndcY < (-1.0f - padNdcY) ||
                   ndcY > (1.0f + padNdcY);
        }

        [[nodiscard]] uint32_t computeTileKey(const RenderCamera&               camera,
                                              const rhi::Extent2D               extent,
                                              const resource::GpuGaussianSplat& splat,
                                              const glm::mat4&                  model)
        {
            if (extent.width == 0u || extent.height == 0u)
                return std::numeric_limits<uint32_t>::max();

            constexpr float kTileSizePx = 16.0f;

            const glm::vec4 worldCenter = model * glm::vec4(splat.center, 1.0f);
            const glm::vec4 viewCenter4 = camera.view * worldCenter;
            const glm::vec3 viewCenter  = glm::vec3(viewCenter4);
            if (viewCenter.z >= -0.02f)
                return std::numeric_limits<uint32_t>::max();

            const float depth = glm::max(-viewCenter.z, 1e-4f);
            const float ndcX  = (camera.projection[0][0] * viewCenter.x) / depth;
            const float ndcY  = (camera.projection[1][1] * viewCenter.y) / depth;

            const float widthF  = static_cast<float>(extent.width);
            const float heightF = static_cast<float>(extent.height);

            const float centerPxX = (ndcX * 0.5f + 0.5f) * widthF;
            const float centerPxY = (ndcY * 0.5f + 0.5f) * heightF;

            const uint32_t tilesX = (extent.width + 15u) / 16u;
            const uint32_t tileX = glm::min(static_cast<uint32_t>(glm::clamp(centerPxX / kTileSizePx,
                                                                              0.0f,
                                                                              glm::max(0.0f, static_cast<float>(tilesX - 1u)))),
                                            tilesX - 1u);
            const uint32_t tilesY = (extent.height + 15u) / 16u;
            const uint32_t tileY = glm::min(static_cast<uint32_t>(glm::clamp(centerPxY / kTileSizePx,
                                                                              0.0f,
                                                                              glm::max(0.0f, static_cast<float>(tilesY - 1u)))),
                                            tilesY - 1u);

            return tileY * tilesX + tileX;
        }

        [[nodiscard]] float computeDrawDepth(const RenderCamera&               camera,
                                             const resource::GpuGaussianSplat& splat,
                                             const glm::mat4&                  model)
        {
            const glm::vec4 worldCenter = model * glm::vec4(splat.center, 1.0f);
            const glm::vec4 viewCenter4 = camera.view * worldCenter;
            return glm::max(-viewCenter4.z, 1e-4f);
        }
    } // namespace

    FrameGraphResource GaussianSplatCullPass::addPass(FrameGraphBuildContext& ctx, FrameGraphResource buildToken)
    {
        struct PassData
        {
            FrameGraphResource camera;
            FrameGraphResource buildToken;
            FrameGraphResource token;
        };

        const auto cameraBlock = ctx.bb.get<CameraData>().cameraBlock.fgResource;

        auto data = ctx.fg.addCallbackPass<PassData>(
            PASS_NAME,
            [cameraBlock, buildToken](FrameGraph::Builder& builder, PassData& pd) {
                PASS_SETUP_ZONE;

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
                                                     .location      = {.set = 0, .binding = 31},
                                                     .pipelineStage = framegraph::PipelineStage::eTransfer,
                                                 });
                }

                pd.token = builder.create<framegraph::FrameGraphBuffer>("GaussianSplatCullToken",
                                                                        {
                                                                            .type = framegraph::BufferType::eStorageBuffer,
                                                                            .stride = sizeof(uint32_t),
                                                                            .capacity = 1,
                                                                        });
                pd.token = builder.write(pd.token,
                                         framegraph::BindingInfo {
                                             .location = {.set = 0, .binding = 31},
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

                auto* gpuSceneView = rc.view().gpuSceneView;
                auto* gpuSceneDatabase = rc.view().gpuSceneDatabase;
                if (gpuSceneView)
                {
                    gpuSceneView->ensureGaussianSplatDrawBuffer(rc.rd);
                    gpuSceneView->ensureGaussianSplatVisibleCountBuffer(rc.rd);
                    gpuSceneView->ensureGaussianSplatIndirectBuffer(rc.rd);
                }
                if (!gpuSceneView || !gpuSceneView->gaussianSplatDrawBuffer || !gpuSceneView->gaussianSplatVisibleCountBuffer ||
                    !gpuSceneView->gaussianSplatIndirectBuffer.has_value())
                    return;

                if (!gpuSceneDatabase || !gpuSceneDatabase->resources)
                    return;

                const auto& splats = gpuSceneDatabase->resources->gaussianSplats;

                auto* cameraUbo = resources.get<framegraph::FrameGraphBuffer>(pd.camera).buffer;
                if (!cameraUbo)
                    return;

                const uint32_t drawCount = gpuSceneView->getDispatchableGaussianSplatDrawCount();
                if (drawCount == 0)
                    return;

                uint32_t totalPointCount = 0;
                m_DrawPointBaseOffsets.assign(drawCount, 0u);
                m_DrawPointCounts.assign(drawCount, 0u);
                m_SortDrawIds.clear();
                m_SortDrawIds.reserve(drawCount);
                struct SortedDraw
                {
                    uint32_t tileKey;
                    float    depth;
                    uint32_t drawId;
                };

                std::vector<SortedDraw> tileKeyDrawPairs;
                tileKeyDrawPairs.reserve(drawCount);

                static bool s_LoggedGaussianSplatCull = false;

                const RenderCamera* camera = rc.view().camera;
                const auto extent = rc.view().extent;

                for (uint32_t drawId = 0; drawId < drawCount; ++drawId)
                {
                    if (drawId >= gpuSceneView->gaussianSplatDraws.size())
                        continue;

                    const uint32_t splatIndex = gpuSceneView->gaussianSplatDraws[drawId].primitiveIndex;
                    if (splatIndex >= splats.size())
                        continue;

                    const auto& splat = splats[splatIndex];
                    const uint32_t pointCount = splat.pointCount;
                    if (pointCount == 0u)
                        continue;

                    m_DrawPointBaseOffsets[drawId] = totalPointCount;
                    m_DrawPointCounts[drawId]      = pointCount;
                    totalPointCount += pointCount;

                    if (camera)
                    {
                        const auto& drawRecord = gpuSceneView->gaussianSplatDraws[drawId];
                        const uint32_t tileKey = computeTileKey(*camera, extent, splat, drawRecord.model);
                        const float depth = computeDrawDepth(*camera, splat, drawRecord.model);
                        tileKeyDrawPairs.push_back(SortedDraw {
                            .tileKey = tileKey,
                            .depth   = depth,
                            .drawId  = drawId,
                        });
                    }
                    else
                    {
                        tileKeyDrawPairs.push_back(SortedDraw {
                            .tileKey = drawId,
                            .depth   = 0.0f,
                            .drawId  = drawId,
                        });
                    }
                }

                if (!tileKeyDrawPairs.empty())
                {
                    std::stable_sort(tileKeyDrawPairs.begin(),
                                     tileKeyDrawPairs.end(),
                                     [](const SortedDraw& a, const SortedDraw& b) {
                                         if (a.tileKey != b.tileKey)
                                             return a.tileKey < b.tileKey;
                                         // Within a tile, render farther splat draws first for more stable alpha compositing.
                                         if (a.depth != b.depth)
                                             return a.depth > b.depth;
                                         return a.drawId < b.drawId;
                                     });
                    for (const auto& kv : tileKeyDrawPairs)
                        m_SortDrawIds.push_back(kv.drawId);
                }

                if (!s_LoggedGaussianSplatCull)
                {
                    VULTRA_CORE_INFO("[GaussianSplat] cull drawCount={} sortDraws={} totalPointCount={}",
                                     drawCount,
                                     m_SortDrawIds.size(),
                                     totalPointCount);
                    s_LoggedGaussianSplatCull = true;
                }

                // Per-frame tiny reset must be recorded in-frame, not submitted synchronously via uploadS.
                rc.cb.clear(*gpuSceneView->gaussianSplatVisibleCountBuffer, 0u);

                rhi::prepareForComputing(rc.cb, *gpuSceneView->gaussianSplatDrawBuffer);
                rhi::prepareForComputing(rc.cb, *gpuSceneView->gaussianSplatVisibleCountBuffer);
                rhi::prepareForComputing(rc.cb, gpuSceneView->gaussianSplatIndirectBuffer.value());

                if (totalPointCount == 0u || m_SortDrawIds.empty())
                {
                    rc.clear();
                    return;
                }

                const bool outputSrgb = rc.view().target ? isSrgbPixelFormat(rc.view().target->getPixelFormat()) : false;

                std::array<const rhi::ComputePipeline*, 4> sortPipelines {nullptr, nullptr, nullptr, nullptr};
                for (uint32_t shDegree = 0u; shDegree < sortPipelines.size(); ++shDegree)
                {
                    std::unordered_map<std::string, uint32_t> shKeywords {
                        {"SPLAT_SH_DEGREE_0", 0u},
                        {"SPLAT_SH_DEGREE_1", 0u},
                        {"SPLAT_SH_DEGREE_2", 0u},
                        {"SPLAT_SH_DEGREE_3", 0u},
                        {"SPLAT_OUTPUT_SRGB", outputSrgb ? 1u : 0u},
                    };
                    switch (shDegree)
                    {
                        case 0u: shKeywords["SPLAT_SH_DEGREE_0"] = 1u; break;
                        case 1u: shKeywords["SPLAT_SH_DEGREE_1"] = 1u; break;
                        case 2u: shKeywords["SPLAT_SH_DEGREE_2"] = 1u; break;
                        case 3u: shKeywords["SPLAT_SH_DEGREE_3"] = 1u; break;
                        default: break;
                    }

                    auto sortVariantHash = getShaderLib().computeVariantHash(
                        "gaussian_splat_sort_keys.comp",
                        vshadersystem::ShaderStage::eComp,
                        shKeywords);
                    sortPipelines[shDegree] = getPipeline(sortVariantHash);
                }
                auto sortFallbackVariantHash = getShaderLib().computeVariantHash(
                    "gaussian_splat_sort_keys.comp",
                    vshadersystem::ShaderStage::eComp,
                    {
                        {"SPLAT_SH_DEGREE_0", 0u},
                        {"SPLAT_SH_DEGREE_1", 0u},
                        {"SPLAT_SH_DEGREE_2", 1u},
                        {"SPLAT_SH_DEGREE_3", 0u},
                        {"SPLAT_OUTPUT_SRGB", outputSrgb ? 1u : 0u},
                    });
                const auto* sortFallbackPipeline = getPipeline(sortFallbackVariantHash);

                if (!m_RadixSorter.has_value() || m_RadixSorterMaxElementCount < totalPointCount)
                {
                    m_RadixSorter = rc.rd.createRadixSorter(totalPointCount);
                    m_RadixSorterMaxElementCount = totalPointCount;
                }

                if (!m_RadixSorter.has_value() || !m_RadixSorter.value())
                    return;

                gpuSceneView->ensureGaussianSplatSortBuffers(rc.rd, m_RadixSorter.value(), totalPointCount);
                if (!gpuSceneView->gaussianSplatSortKeysBuffer || !gpuSceneView->gaussianSplatSortValuesBuffer ||
                    !gpuSceneView->gaussianSplatSortStorageBuffer || !gpuSceneView->gaussianSplatProjectedBuffer)
                    return;

                rhi::prepareForComputing(rc.cb, *gpuSceneView->gaussianSplatSortKeysBuffer);
                rhi::prepareForComputing(rc.cb, *gpuSceneView->gaussianSplatSortValuesBuffer);
                rhi::prepareForComputing(rc.cb, *gpuSceneView->gaussianSplatSortStorageBuffer);
                rhi::prepareForComputing(rc.cb, *gpuSceneView->gaussianSplatProjectedBuffer);

                const rhi::StorageBuffer* lastCentersBuffer = nullptr;
                const rhi::StorageBuffer* lastColorBuffer = nullptr;
                const rhi::StorageBuffer* lastCovarianceBuffer = nullptr;
                const rhi::StorageBuffer* lastShBuffer = nullptr;

                for (uint32_t drawId : m_SortDrawIds)
                {
                    if (drawId >= gpuSceneView->gaussianSplatDraws.size())
                        continue;

                    const uint32_t splatIndex = gpuSceneView->gaussianSplatDraws[drawId].primitiveIndex;
                    if (splatIndex >= splats.size())
                        continue;

                    const auto& splat = splats[splatIndex];
                    const uint32_t pointCount = m_DrawPointCounts[drawId];
                    const uint32_t valueBase  = m_DrawPointBaseOffsets[drawId];
                    if (pointCount == 0u || !splat.centersBuffer || !splat.colorBuffer || !splat.covarianceBuffer ||
                        !splat.shBuffer)
                        continue;

                    if (splat.centersBuffer.get() != lastCentersBuffer)
                    {
                        rhi::prepareForComputing(rc.cb, *splat.centersBuffer);
                        lastCentersBuffer = splat.centersBuffer.get();
                    }
                    if (splat.colorBuffer.get() != lastColorBuffer)
                    {
                        rhi::prepareForComputing(rc.cb, *splat.colorBuffer);
                        lastColorBuffer = splat.colorBuffer.get();
                    }
                    if (splat.covarianceBuffer.get() != lastCovarianceBuffer)
                    {
                        rhi::prepareForComputing(rc.cb, *splat.covarianceBuffer);
                        lastCovarianceBuffer = splat.covarianceBuffer.get();
                    }
                    if (splat.shBuffer.get() != lastShBuffer)
                    {
                        rhi::prepareForComputing(rc.cb, *splat.shBuffer);
                        lastShBuffer = splat.shBuffer.get();
                    }

                    rc.resourceSet[0] = {
                        {0, rhi::bindings::UniformBuffer {.buffer = cameraUbo}},
                        {1, rhi::bindings::StorageBuffer {.buffer = gpuSceneView->gaussianSplatDrawBuffer.get()}},
                        {13, rhi::bindings::StorageBuffer {.buffer = splat.centersBuffer.get()}},
                        {14, rhi::bindings::StorageBuffer {.buffer = splat.covarianceBuffer.get()}},
                        {15, rhi::bindings::StorageBuffer {.buffer = splat.colorBuffer.get()}},
                        {16, rhi::bindings::StorageBuffer {.buffer = splat.shBuffer.get()}},
                        {17, rhi::bindings::StorageBuffer {.buffer = gpuSceneView->gaussianSplatSortKeysBuffer.get()}},
                        {18, rhi::bindings::StorageBuffer {.buffer = gpuSceneView->gaussianSplatSortValuesBuffer.get()}},
                        {19, rhi::bindings::StorageBuffer {.buffer = gpuSceneView->gaussianSplatProjectedBuffer.get()}},
                        {20, rhi::bindings::StorageBuffer {.buffer = gpuSceneView->gaussianSplatVisibleCountBuffer.get()}},
                    };

                    SortKeysPushConstants pc {};
                    pc.drawId     = drawId;
                    pc.pointCount = pointCount;
                    pc.valueBase  = valueBase;
                    pc.shDegree   = std::min<uint32_t>(splat.shDegree < 0 ? 0 : static_cast<uint32_t>(splat.shDegree), 3u);

                    const auto* sortPipeline = sortPipelines[pc.shDegree];
                    if (!sortPipeline)
                        sortPipeline = sortFallbackPipeline;
                    if (!sortPipeline)
                        continue;

                    {
                        RHI_GPU_ZONE(rc.cb, "GaussianSplatCullPass::SortKeys");
                        rc.cb.bindPipeline(*sortPipeline);
                        rc.bindDescriptorSets(*sortPipeline);
                        rc.cb.pushConstants(rhi::ShaderStages::eCompute, 0, &pc);
                        rc.cb.dispatch({(pointCount + 63u) / 64u, 1u, 1u});
                        rc.cb.insertComputeUavBarrier();
                    }

                }

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

                auto writeIndirectVariantHash =
                    getShaderLib().computeVariantHash("gaussian_splat_write_indirect.comp", vshadersystem::ShaderStage::eComp, {});
                const auto* writeIndirectPipeline = getPipeline(writeIndirectVariantHash);
                if (writeIndirectPipeline)
                {
                    rc.resourceSet[0] = {
                        {12, rhi::bindings::StorageBuffer {.buffer = &gpuSceneView->gaussianSplatIndirectBuffer.value()}},
                        {20, rhi::bindings::StorageBuffer {.buffer = gpuSceneView->gaussianSplatVisibleCountBuffer.get()}},
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
