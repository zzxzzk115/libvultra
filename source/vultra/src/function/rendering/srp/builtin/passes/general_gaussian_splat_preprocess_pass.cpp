#include "vultra/function/rendering/srp/builtin/passes/general_gaussian_splat_preprocess_pass.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/function/framegraph/framegraph_buffer.hpp"
#include "vultra/function/framegraph/framegraph_import.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/rendering/srp/builtin/resource_keys.hpp"

#include <glm/vec4.hpp>

#include <fg/FrameGraph.hpp>

#include <algorithm>
#include <array>
#include <initializer_list>
#include <vector>

namespace vultra
{
    namespace
    {
        constexpr auto PASS_NAME = "GeneralGaussianSplatPreprocessPass";
        constexpr auto kStereoCameraBinding = 23u;
        constexpr auto kFoveatedLayerCount = resource::kGeneralGaussianSplatFoveatedLayerCount;
        constexpr std::array<uint32_t, kFoveatedLayerCount> kFoveatedVisibleSplatBindings {31u, 32u, 33u};
        constexpr std::array<uint32_t, kFoveatedLayerCount> kFoveatedSortKeyBindings {34u, 35u, 36u};
        constexpr std::array<uint32_t, kFoveatedLayerCount> kFoveatedSortIndexBindings {37u, 38u, 39u};
        constexpr std::array<uint32_t, kFoveatedLayerCount> kFoveatedVisibleCountBindings {40u, 41u, 42u};
        constexpr std::array<uint32_t, kFoveatedLayerCount> kFoveatedIndirectBindings {43u, 44u, 45u};

        struct GeneralGaussianSplatPreprocessUniforms
        {
            uint32_t pointCount {0};
            uint32_t maxVisibleSplats {0};
            uint32_t rankTotalCount {0};
            uint32_t foveatedClodEnabled {0};
            glm::vec4 foveatedGazeAndRings {0.5f, 0.5f, 5.0f, 15.0f};
            glm::vec4 foveatedLevelsAndTransition {1.0f, 0.25f, 0.05f, 2.0f};
        };

        struct GeneralGaussianSplatFoveatedClodPushParams
        {
            uint32_t  enabled {0};
            glm::vec4 gazeAndRings {0.5f, 0.5f, 5.0f, 15.0f};
            glm::vec4 levelsAndTransition {1.0f, 0.25f, 0.05f, 2.0f};
        };

        GeneralGaussianSplatFoveatedClodPushParams makeFoveatedClodPushParams(
            const resource::GpuSceneView& gpuSceneView)
        {
            GeneralGaussianSplatFoveatedClodPushParams params {};
            params.enabled = gpuSceneView.generalGaussianSplatFoveatedClodEnabled ? 1u : 0u;
            params.gazeAndRings = glm::vec4 {gpuSceneView.generalGaussianSplatFoveatedGaze.x,
                                             gpuSceneView.generalGaussianSplatFoveatedGaze.y,
                                             gpuSceneView.generalGaussianSplatFoveatedRingDegrees.x,
                                             gpuSceneView.generalGaussianSplatFoveatedRingDegrees.y};
            params.levelsAndTransition =
                glm::vec4 {gpuSceneView.generalGaussianSplatFoveatedRingLevels.x,
                           gpuSceneView.generalGaussianSplatFoveatedRingLevels.y,
                           gpuSceneView.generalGaussianSplatFoveatedRingLevels.z,
                           std::max(gpuSceneView.generalGaussianSplatFoveatedTransitionDegrees, 0.0f)};
            return params;
        }

    } // namespace

    GeneralGaussianSplatPreprocessPass::GeneralGaussianSplatPreprocessPass() { setShaderProfile(rhi::ShaderProfile::eGeneral); }

    void GeneralGaussianSplatPreprocessPass::addPass(FrameGraphBuildContext& ctx)
    {
        auto* gpuSceneView = ctx.view().gpuSceneView;
        if (!gpuSceneView)
            return;
        if (!gpuSceneView->hasGeneralGaussianSplats())
            return;

        const auto cameraBlock       = ctx.bb.get<CameraData>().cameraBlock.fgResource;
        const auto stereoCameraBlock = ctx.bb.get<CameraData>().stereoCameraBlock.fgResource;
        const bool useMultiview =
            ctx.view().enableMultiview && ctx.view().multiviewCameraCount >= 2u && static_cast<bool>(stereoCameraBlock);
        const bool useDirectPrefix = gpuSceneView->generalGaussianSplatDirectPrefix;
        const bool useFoveatedLayerOutput = gpuSceneView->generalGaussianSplatFoveatedLayeredCompositeEnabled &&
                                            ctx.rd.getBackendApi() != rhi::RenderBackendApi::eWebGPU;

        if (gpuSceneView->generalGaussianSplatDrawBuffer)
        {
            ctx.data.set(kResKey_GeneralGaussianSplatDrawBuffer,
                         framegraph::importBuffer(ctx.fg,
                                                  "GeneralGaussianSplatDrawBuffer",
                                                  gpuSceneView->generalGaussianSplatDrawBuffer.get(),
                                                  framegraph::BufferType::eStorageBuffer,
                                                  sizeof(resource::GpuGeneralGaussianSplatDrawRecord)));
        }

        if (gpuSceneView->generalGaussianSplatPackedSourceBuffer)
        {
            ctx.data.set(kResKey_GeneralGaussianSplatPackedSourceBuffer,
                         framegraph::importBuffer(ctx.fg,
                                                  "GeneralGaussianSplatPackedSourceBuffer",
                                                  gpuSceneView->generalGaussianSplatPackedSourceBuffer.get(),
                                                  framegraph::BufferType::eStorageBuffer,
                                                  sizeof(resource::GpuGeneralGaussianSplatPackedSource)));
        }

        if (!useDirectPrefix && gpuSceneView->generalGaussianSplatSelectedSourceBuffer)
        {
            ctx.data.set(kResKey_GeneralGaussianSplatSelectedSourceBuffer,
                         framegraph::importBuffer(ctx.fg,
                                                  "GeneralGaussianSplatSelectedSourceBuffer",
                                                  gpuSceneView->generalGaussianSplatSelectedSourceBuffer.get(),
                                                  framegraph::BufferType::eStorageBuffer,
                                                  sizeof(resource::GpuGeneralGaussianSplatSelectedSource)));
        }

        if (gpuSceneView->generalGaussianSplatVisibleSplatBuffer)
        {
            ctx.data.set(kResKey_GeneralGaussianSplatVisibleSplatBuffer,
                         framegraph::importBuffer(ctx.fg,
                                                  "GeneralGaussianSplatVisibleSplatBuffer",
                                                  gpuSceneView->generalGaussianSplatVisibleSplatBuffer.get(),
                                                  framegraph::BufferType::eStorageBuffer,
                                                  sizeof(resource::GpuGeneralGaussianSplatVisibleSplat)));
        }

        if (gpuSceneView->generalGaussianSplatSortKeyBuffer)
        {
            ctx.data.set(kResKey_GeneralGaussianSplatSortKeyBuffer,
                         framegraph::importBuffer(ctx.fg,
                                                  "GeneralGaussianSplatSortKeyBuffer",
                                                  gpuSceneView->generalGaussianSplatSortKeyBuffer.get(),
                                                  framegraph::BufferType::eStorageBuffer,
                                                  sizeof(uint32_t)));
        }

        if (gpuSceneView->generalGaussianSplatSortIndexBuffer)
        {
            ctx.data.set(kResKey_GeneralGaussianSplatSortIndexBuffer,
                         framegraph::importBuffer(ctx.fg,
                                                  "GeneralGaussianSplatSortIndexBuffer",
                                                  gpuSceneView->generalGaussianSplatSortIndexBuffer.get(),
                                                  framegraph::BufferType::eStorageBuffer,
                                                  sizeof(uint32_t)));
        }

        if (gpuSceneView->generalGaussianSplatVisibleCountBuffer)
        {
            ctx.data.set(kResKey_GeneralGaussianSplatVisibleCountBuffer,
                         framegraph::importBuffer(ctx.fg,
                                                  "GeneralGaussianSplatVisibleCountBuffer",
                                                  gpuSceneView->generalGaussianSplatVisibleCountBuffer.get(),
                                                  framegraph::BufferType::eStorageBuffer,
                                                  sizeof(uint32_t)));
        }

        if (gpuSceneView->generalGaussianSplatIndirectBuffer.has_value())
        {
            ctx.data.set(kResKey_GeneralGaussianSplatIndirectBuffer,
                         framegraph::importBuffer(ctx.fg,
                                                  "GeneralGaussianSplatIndirectBuffer",
                                                  &gpuSceneView->generalGaussianSplatIndirectBuffer.value(),
                                                  framegraph::BufferType::eDrawIndirectBuffer,
                                                  sizeof(rhi::DrawIndirectCommand)));
        }

        if (useFoveatedLayerOutput)
        {
            for (uint32_t layer = 0u; layer < kFoveatedLayerCount; ++layer)
            {
                if (gpuSceneView->generalGaussianSplatFoveatedVisibleSplatBuffers[layer])
                {
                    ctx.data.set(kResKey_GeneralGaussianSplatFoveatedVisibleSplatBuffers[layer],
                                 framegraph::importBuffer(
                                     ctx.fg,
                                     "GeneralGaussianSplatFoveatedVisibleSplatBuffer",
                                     gpuSceneView->generalGaussianSplatFoveatedVisibleSplatBuffers[layer].get(),
                                     framegraph::BufferType::eStorageBuffer,
                                     sizeof(resource::GpuGeneralGaussianSplatVisibleSplat)));
                }
                if (gpuSceneView->generalGaussianSplatFoveatedSortKeyBuffers[layer])
                {
                    ctx.data.set(kResKey_GeneralGaussianSplatFoveatedSortKeyBuffers[layer],
                                 framegraph::importBuffer(
                                     ctx.fg,
                                     "GeneralGaussianSplatFoveatedSortKeyBuffer",
                                     gpuSceneView->generalGaussianSplatFoveatedSortKeyBuffers[layer].get(),
                                     framegraph::BufferType::eStorageBuffer,
                                     sizeof(uint32_t)));
                }
                if (gpuSceneView->generalGaussianSplatFoveatedSortIndexBuffers[layer])
                {
                    ctx.data.set(kResKey_GeneralGaussianSplatFoveatedSortIndexBuffers[layer],
                                 framegraph::importBuffer(
                                     ctx.fg,
                                     "GeneralGaussianSplatFoveatedSortIndexBuffer",
                                     gpuSceneView->generalGaussianSplatFoveatedSortIndexBuffers[layer].get(),
                                     framegraph::BufferType::eStorageBuffer,
                                     sizeof(uint32_t)));
                }
                if (gpuSceneView->generalGaussianSplatFoveatedVisibleCountBuffers[layer])
                {
                    ctx.data.set(kResKey_GeneralGaussianSplatFoveatedVisibleCountBuffers[layer],
                                 framegraph::importBuffer(
                                     ctx.fg,
                                     "GeneralGaussianSplatFoveatedVisibleCountBuffer",
                                     gpuSceneView->generalGaussianSplatFoveatedVisibleCountBuffers[layer].get(),
                                     framegraph::BufferType::eStorageBuffer,
                                     sizeof(uint32_t)));
                }
                if (gpuSceneView->generalGaussianSplatFoveatedIndirectBuffers[layer].has_value())
                {
                    ctx.data.set(kResKey_GeneralGaussianSplatFoveatedIndirectBuffers[layer],
                                 framegraph::importBuffer(
                                     ctx.fg,
                                     "GeneralGaussianSplatFoveatedIndirectBuffer",
                                     &gpuSceneView->generalGaussianSplatFoveatedIndirectBuffers[layer].value(),
                                     framegraph::BufferType::eDrawIndirectBuffer,
                                     sizeof(rhi::DrawIndirectCommand)));
                }
            }
        }

        if (gpuSceneView->generalGaussianSplatSortStorageBuffer)
        {
            ctx.data.set(kResKey_GeneralGaussianSplatSortStorageBuffer,
                         framegraph::importBuffer(ctx.fg,
                                                  "GeneralGaussianSplatSortStorageBuffer",
                                                  gpuSceneView->generalGaussianSplatSortStorageBuffer.get(),
                                                  framegraph::BufferType::eStorageBuffer,
                                                  sizeof(uint32_t)));
        }

        if (gpuSceneView->generalGaussianSplatShBuffer)
        {
            ctx.data.set(kResKey_GeneralGaussianSplatShBuffer,
                         framegraph::importBuffer(ctx.fg,
                                                  "GeneralGaussianSplatShBuffer",
                                                  gpuSceneView->generalGaussianSplatShBuffer.get(),
                                                  framegraph::BufferType::eStorageBuffer,
                                                  sizeof(glm::uvec2)));
        }

        auto packedSourceBuffer = ctx.data.tryGet(kResKey_GeneralGaussianSplatPackedSourceBuffer);
        auto selectedSourceBuffer = ctx.data.tryGet(kResKey_GeneralGaussianSplatSelectedSourceBuffer);
        auto drawBuffer         = ctx.data.tryGet(kResKey_GeneralGaussianSplatDrawBuffer);
        auto visibleSplatBuffer = ctx.data.tryGet(kResKey_GeneralGaussianSplatVisibleSplatBuffer);
        auto sortKeyBuffer      = ctx.data.tryGet(kResKey_GeneralGaussianSplatSortKeyBuffer);
        auto sortIndexBuffer    = ctx.data.tryGet(kResKey_GeneralGaussianSplatSortIndexBuffer);
        auto visibleCountBuffer = ctx.data.tryGet(kResKey_GeneralGaussianSplatVisibleCountBuffer);
        auto indirectBuffer     = ctx.data.tryGet(kResKey_GeneralGaussianSplatIndirectBuffer);
        auto sortStorageBuffer  = ctx.data.tryGet(kResKey_GeneralGaussianSplatSortStorageBuffer);
        auto shBuffer           = ctx.data.tryGet(kResKey_GeneralGaussianSplatShBuffer);
        std::array<FrameGraphResource, kFoveatedLayerCount> foveatedVisibleSplatBuffers {};
        std::array<FrameGraphResource, kFoveatedLayerCount> foveatedSortKeyBuffers {};
        std::array<FrameGraphResource, kFoveatedLayerCount> foveatedSortIndexBuffers {};
        std::array<FrameGraphResource, kFoveatedLayerCount> foveatedVisibleCountBuffers {};
        std::array<FrameGraphResource, kFoveatedLayerCount> foveatedIndirectBuffers {};
        for (uint32_t layer = 0u; layer < kFoveatedLayerCount; ++layer)
        {
            foveatedVisibleSplatBuffers[layer] =
                ctx.data.tryGet(kResKey_GeneralGaussianSplatFoveatedVisibleSplatBuffers[layer]);
            foveatedSortKeyBuffers[layer] =
                ctx.data.tryGet(kResKey_GeneralGaussianSplatFoveatedSortKeyBuffers[layer]);
            foveatedSortIndexBuffers[layer] =
                ctx.data.tryGet(kResKey_GeneralGaussianSplatFoveatedSortIndexBuffers[layer]);
            foveatedVisibleCountBuffers[layer] =
                ctx.data.tryGet(kResKey_GeneralGaussianSplatFoveatedVisibleCountBuffers[layer]);
            foveatedIndirectBuffers[layer] =
                ctx.data.tryGet(kResKey_GeneralGaussianSplatFoveatedIndirectBuffers[layer]);
        }

        const uint32_t pointCount = gpuSceneView->activeGeneralGaussianSplatPoints;
        const uint32_t maxVisible = gpuSceneView->maxGeneralGaussianSplatVisibleSplats;
        const uint32_t rankTotalCount =
            useDirectPrefix ? gpuSceneView->maxGeneralGaussianSplatSourceCount :
                              gpuSceneView->maxGeneralGaussianSplatPoints;
        const auto foveatedClodParams = makeFoveatedClodPushParams(*gpuSceneView);
        GeneralGaussianSplatPreprocessUniforms uniformsData {};
        uniformsData.pointCount            = pointCount;
        uniformsData.maxVisibleSplats      = maxVisible;
        uniformsData.rankTotalCount        = rankTotalCount;
        uniformsData.foveatedClodEnabled   = foveatedClodParams.enabled;
        uniformsData.foveatedGazeAndRings  = foveatedClodParams.gazeAndRings;
        uniformsData.foveatedLevelsAndTransition = foveatedClodParams.levelsAndTransition;
        if (!m_UniformBuffer || m_UniformBuffer.getSize() < sizeof(GeneralGaussianSplatPreprocessUniforms))
        {
            m_UniformBuffer = ctx.rd.createUniformBuffer(sizeof(GeneralGaussianSplatPreprocessUniforms));
        }

        auto hasAllFoveatedLayerResources = [&]() {
            for (uint32_t layer = 0u; layer < kFoveatedLayerCount; ++layer)
            {
                if (!foveatedVisibleSplatBuffers[layer] || !foveatedSortKeyBuffers[layer] ||
                    !foveatedSortIndexBuffers[layer] || !foveatedVisibleCountBuffers[layer] ||
                    !foveatedIndirectBuffers[layer])
                {
                    return false;
                }
            }
            return true;
        };

        const bool hasOutputResources =
            useFoveatedLayerOutput ?
                hasAllFoveatedLayerResources() :
                static_cast<bool>(visibleSplatBuffer && sortKeyBuffer && sortIndexBuffer && visibleCountBuffer &&
                                  indirectBuffer);

        if (!cameraBlock || !drawBuffer || !packedSourceBuffer || !hasOutputResources || !sortStorageBuffer ||
            !shBuffer || (!useDirectPrefix && !selectedSourceBuffer))
        {
            return;
        }

        struct PassData
        {
            FrameGraphResource camera;
            FrameGraphResource stereoCamera;
            FrameGraphResource drawBuffer;
            FrameGraphResource packedSourceBuffer;
            FrameGraphResource selectedSourceBuffer;
            FrameGraphResource visibleSplatBuffer;
            FrameGraphResource sortKeyBuffer;
            FrameGraphResource sortIndexBuffer;
            FrameGraphResource visibleCountBuffer;
            FrameGraphResource indirectBuffer;
            FrameGraphResource sortStorageBuffer;
            FrameGraphResource shBuffer;
            std::array<FrameGraphResource, kFoveatedLayerCount> foveatedVisibleSplatBuffers;
            std::array<FrameGraphResource, kFoveatedLayerCount> foveatedSortKeyBuffers;
            std::array<FrameGraphResource, kFoveatedLayerCount> foveatedSortIndexBuffers;
            std::array<FrameGraphResource, kFoveatedLayerCount> foveatedVisibleCountBuffers;
            std::array<FrameGraphResource, kFoveatedLayerCount> foveatedIndirectBuffers;
        };

        ctx.fg.addCallbackPass<PassData>(
            PASS_NAME,
            [cameraBlock,
             stereoCameraBlock,
             useMultiview,
             drawBuffer,
             packedSourceBuffer,
             visibleSplatBuffer,
             sortKeyBuffer,
             sortIndexBuffer,
             visibleCountBuffer,
             indirectBuffer,
             sortStorageBuffer,
             shBuffer,
             foveatedVisibleSplatBuffers,
             foveatedSortKeyBuffers,
             foveatedSortIndexBuffers,
             foveatedVisibleCountBuffers,
             foveatedIndirectBuffers,
             selectedSourceBuffer,
             useDirectPrefix,
             useFoveatedLayerOutput](FrameGraph::Builder& builder, PassData& data) {
                PASS_SETUP_ZONE;

                data.camera             = builder.read(cameraBlock,
                                           framegraph::BindingInfo {
                                                           .location      = {.set = 0, .binding = 0},
                                                           .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                           });
                if (useMultiview)
                {
                    data.stereoCamera = builder.read(stereoCameraBlock,
                                                     framegraph::BindingInfo {
                                                         .location      = {.set = 0, .binding = kStereoCameraBinding},
                                                         .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                                     });
                }
                data.drawBuffer         = builder.read(drawBuffer,
                                               framegraph::BindingInfo {
                                                           .location      = {.set = 0, .binding = 13},
                                                           .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                               });
                data.packedSourceBuffer = builder.read(packedSourceBuffer,
                                                       framegraph::BindingInfo {
                                                           .location      = {.set = 0, .binding = 14},
                                                           .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                                       });
                if (!useDirectPrefix)
                {
                    data.selectedSourceBuffer =
                        builder.read(selectedSourceBuffer,
                                     framegraph::BindingInfo {
                                         .location      = {.set = 0, .binding = 27},
                                         .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                     });
                }
                if (useFoveatedLayerOutput)
                {
                    for (uint32_t layer = 0u; layer < kFoveatedLayerCount; ++layer)
                    {
                        data.foveatedVisibleSplatBuffers[layer] =
                            builder.write(foveatedVisibleSplatBuffers[layer],
                                          framegraph::BindingInfo {
                                              .location      = {.set = 0, .binding = kFoveatedVisibleSplatBindings[layer]},
                                              .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                          });
                        data.foveatedSortKeyBuffers[layer] =
                            builder.write(foveatedSortKeyBuffers[layer],
                                          framegraph::BindingInfo {
                                              .location      = {.set = 0, .binding = kFoveatedSortKeyBindings[layer]},
                                              .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                          });
                        data.foveatedSortIndexBuffers[layer] =
                            builder.write(foveatedSortIndexBuffers[layer],
                                          framegraph::BindingInfo {
                                              .location      = {.set = 0, .binding = kFoveatedSortIndexBindings[layer]},
                                              .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                          });
                        data.foveatedVisibleCountBuffers[layer] =
                            builder.write(foveatedVisibleCountBuffers[layer],
                                          framegraph::BindingInfo {
                                              .location      = {.set = 0, .binding = kFoveatedVisibleCountBindings[layer]},
                                              .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                          });
                        data.foveatedIndirectBuffers[layer] =
                            builder.write(foveatedIndirectBuffers[layer],
                                          framegraph::BindingInfo {
                                              .location      = {.set = 0, .binding = kFoveatedIndirectBindings[layer]},
                                              .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                          });
                    }
                }
                else
                {
                    data.visibleSplatBuffer =
                        builder.write(visibleSplatBuffer,
                                      framegraph::BindingInfo {
                                          .location      = {.set = 0, .binding = 15},
                                          .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                      });
                    data.sortKeyBuffer =
                        builder.write(sortKeyBuffer,
                                      framegraph::BindingInfo {
                                          .location      = {.set = 0, .binding = 16},
                                          .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                      });
                    data.sortIndexBuffer =
                        builder.write(sortIndexBuffer,
                                      framegraph::BindingInfo {
                                          .location      = {.set = 0, .binding = 17},
                                          .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                      });
                    data.visibleCountBuffer =
                        builder.write(visibleCountBuffer,
                                      framegraph::BindingInfo {
                                          .location      = {.set = 0, .binding = 18},
                                          .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                      });
                    data.indirectBuffer =
                        builder.write(indirectBuffer,
                                      framegraph::BindingInfo {
                                          .location      = {.set = 0, .binding = 20},
                                          .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                      });
                }
                data.sortStorageBuffer = builder.write(sortStorageBuffer,
                                                       framegraph::BindingInfo {
                                                           .location      = {.set = 0, .binding = 21},
                                                           .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                                       });
                data.shBuffer          = builder.read(shBuffer,
                                             framegraph::BindingInfo {
                                                          .location      = {.set = 0, .binding = 22},
                                                          .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                             });
            },
            [this,
             pointCount,
             maxVisible,
             uniformsData,
             useMultiview,
             useDirectPrefix,
             useFoveatedLayerOutput](
                const PassData& data, FrameGraphPassResources& resources, void* ctxPtr) {
                VULTRA_SCOPED_FRAMEGRAPH_EXEC_CONTEXT(rc, ctxPtr);
                setRenderDevice(rc.rd);
                if (!rc.ext.builtinShaderLib)
                {
                    return;
                }
                setShaderLib(*rc.ext.builtinShaderLibForProfile(getShaderProfile()));

                auto bindSubset = [&rc](const rhi::BasePipeline& pipeline, const auto& bindings, const bool includeUniforms = false) {
                    auto saved = rc.resourceSet;
                    rc.resourceSet.clear();

                    if (const auto setIt = saved.find(0); setIt != saved.end())
                    {
                        for (const auto binding : bindings)
                        {
                            if (const auto bindingIt = setIt->second.find(binding); bindingIt != setIt->second.end())
                            {
                                rc.resourceSet[0][binding] = bindingIt->second;
                            }
                        }
                    }
                    if (includeUniforms)
                    {
                        if (const auto setIt = saved.find(1); setIt != saved.end())
                        {
                            rc.resourceSet[1] = setIt->second;
                        }
                    }

                    rc.bindDescriptorSets(pipeline);
                    rc.resourceSet = std::move(saved);
                };

                RHI_GPU_ZONE(rc.cb, PASS_NAME);

                auto* gpuSceneView = rc.view().gpuSceneView;
                if (!gpuSceneView || pointCount == 0u || maxVisible == 0u)
                {
                    return;
                }

                auto* visibleCountBuf = useFoveatedLayerOutput ?
                                            nullptr :
                                            resources.get<framegraph::FrameGraphBuffer>(data.visibleCountBuffer).buffer;
                std::array<rhi::Buffer*, kFoveatedLayerCount> foveatedVisibleCountBufs {};
                if (useFoveatedLayerOutput)
                {
                    for (uint32_t layer = 0u; layer < kFoveatedLayerCount; ++layer)
                    {
                        foveatedVisibleCountBufs[layer] =
                            resources.get<framegraph::FrameGraphBuffer>(data.foveatedVisibleCountBuffers[layer]).buffer;
                    }
                }
                {
                    RHI_GPU_ZONE(rc.cb, "GeneralGaussianSplatPreprocess::ProjectCull");

                    if (useFoveatedLayerOutput)
                    {
                        const uint32_t zero = 0u;
                        for (auto* countBuf : foveatedVisibleCountBufs)
                            rc.cb.update(*countBuf, 0u, sizeof(uint32_t), &zero);
                    }
                    else
                    {
                        const uint32_t zero = 0u;
                        rc.cb.update(*visibleCountBuf, 0u, sizeof(uint32_t), &zero);
                    }
                    rc.cb.getBarrierBuilder().memoryBarrier(
                        {
                            .srcStage  = rhi::PipelineStages::eTransfer,
                            .srcAccess = rhi::Access::eTransferWrite,
                        },
                        {
                            .dstStage  = rhi::PipelineStages::eComputeShader,
                            .dstAccess = rhi::Access::eShaderRead | rhi::Access::eShaderWrite,
                        });

                    const auto* preprocessPipeline = getPipeline(useMultiview, useDirectPrefix, useFoveatedLayerOutput);
                    if (!preprocessPipeline)
                    {
                        return;
                    }
                    rc.cb.update(m_UniformBuffer, 0, sizeof(GeneralGaussianSplatPreprocessUniforms), &uniformsData);
                    rc.resourceSet[1][30] = rhi::bindings::UniformBuffer {.buffer = &m_UniformBuffer};
                    rc.cb.bindPipeline(*preprocessPipeline);
                    std::vector<uint32_t> preprocessBindings {0u, 13u, 14u, 22u};
                    if (useFoveatedLayerOutput)
                    {
                        preprocessBindings.insert(preprocessBindings.end(),
                                                  {31u, 32u, 33u, 34u, 35u, 36u, 37u,
                                                   38u, 39u, 40u, 41u, 42u, 43u, 44u, 45u});
                    }
                    else
                    {
                        preprocessBindings.insert(preprocessBindings.end(), {15u, 16u, 17u, 18u});
                    }
                    if (!useDirectPrefix)
                    {
                        preprocessBindings.push_back(27u);
                    }
                    if (useMultiview)
                    {
                        preprocessBindings.push_back(kStereoCameraBinding);
                    }
                    bindSubset(*preprocessPipeline, preprocessBindings, true);
                    const uint32_t dispatchX = (pointCount + 255u) / 256u;
                    rc.cb.dispatch({dispatchX, 1u, 1u});
                    rc.cb.insertComputeUavBarrier();
                }

                auto* sortStorageBuf = resources.get<framegraph::FrameGraphBuffer>(data.sortStorageBuffer).buffer;

                if (useFoveatedLayerOutput && gpuSceneView->generalGaussianSplatSorter.has_value() &&
                    static_cast<bool>(*gpuSceneView->generalGaussianSplatSorter))
                {
                    RHI_GPU_ZONE(rc.cb, "GeneralGaussianSplatPreprocess::Sort");

                    for (uint32_t layer = 0u; layer < kFoveatedLayerCount; ++layer)
                    {
                        auto* countBuf     = foveatedVisibleCountBufs[layer];
                        auto* sortKeyBuf   =
                            resources.get<framegraph::FrameGraphBuffer>(data.foveatedSortKeyBuffers[layer]).buffer;
                        auto* sortIndexBuf =
                            resources.get<framegraph::FrameGraphBuffer>(data.foveatedSortIndexBuffers[layer]).buffer;

                        gpuSceneView->generalGaussianSplatSorter->sortKeyValuesIndirect(rc.cb,
                                                                                        maxVisible,
                                                                                        *countBuf,
                                                                                        0u,
                                                                                        *sortKeyBuf,
                                                                                        0u,
                                                                                        *sortIndexBuf,
                                                                                        0u,
                                                                                        *sortStorageBuf,
                                                                                        0u);
                        rc.cb.insertComputeUavBarrier();
                    }
                }
                else if (gpuSceneView->generalGaussianSplatSorter.has_value() &&
                         static_cast<bool>(*gpuSceneView->generalGaussianSplatSorter))
                {
                    auto* sortKeyBuf   = resources.get<framegraph::FrameGraphBuffer>(data.sortKeyBuffer).buffer;
                    auto* sortIndexBuf = resources.get<framegraph::FrameGraphBuffer>(data.sortIndexBuffer).buffer;
                    RHI_GPU_ZONE(rc.cb, "GeneralGaussianSplatPreprocess::Sort");

                    gpuSceneView->generalGaussianSplatSorter->sortKeyValuesIndirect(rc.cb,
                                                                                    maxVisible,
                                                                                    *visibleCountBuf,
                                                                                    0u,
                                                                                    *sortKeyBuf,
                                                                                    0u,
                                                                                    *sortIndexBuf,
                                                                                    0u,
                                                                                    *sortStorageBuf,
                                                                                    0u);
                    rc.cb.insertComputeUavBarrier();
                }

                {
                    RHI_GPU_ZONE(rc.cb, "GeneralGaussianSplatPreprocess::WriteIndirect");

                    rhi::ShaderLibraryRuntime::KeywordValues writeIndirectKeywords {
                        {"USE_FOVEATED_LAYER_OUTPUT", useFoveatedLayerOutput ? 1u : 0u},
                    };
                    auto writeIndirectVariantHash =
                        computeGeneralVariantHash("gaussian_splat_write_indirect.comp",
                                                  vshadersystem::ShaderStage::eComp,
                                                  writeIndirectKeywords);
                    const auto* writeIndirectPipeline = getPipeline(writeIndirectVariantHash);
                    if (!writeIndirectPipeline)
                    {
                        return;
                    }

                    rc.cb.bindPipeline(*writeIndirectPipeline);
                    if (useFoveatedLayerOutput)
                    {
                        bindSubset(*writeIndirectPipeline,
                                   std::initializer_list<uint32_t> {31u, 32u, 33u, 34u, 35u, 36u, 37u,
                                                                    38u, 39u, 40u, 41u, 42u, 43u, 44u, 45u});
                    }
                    else
                    {
                        bindSubset(*writeIndirectPipeline, std::initializer_list<uint32_t> {18u, 20u});
                    }
                    rc.cb.dispatch({1u, 1u, 1u});
                    rc.cb.insertComputeUavBarrier();
                }
            });
    }

    rhi::ComputePipeline GeneralGaussianSplatPreprocessPass::createPipeline(const bool useMultiview,
                                                                            const bool useDirectPrefix,
                                                                            const bool useFoveatedLayerOutput) const
    {
        rhi::ShaderLibraryRuntime::KeywordValues keywords {
            {"USE_MULTIVIEW", useMultiview ? 1u : 0u},
            {"USE_DIRECT_PREFIX", useDirectPrefix ? 1u : 0u},
            {"USE_FOVEATED_LAYER_OUTPUT", useFoveatedLayerOutput ? 1u : 0u},
        };
        auto shader = loadGeneralShader("gaussian_splat_preprocess.comp", vshadersystem::ShaderStage::eComp, keywords);
        if (!shader)
            return {};

        if (getRenderDevice().getBackendApi() == rhi::RenderBackendApi::eWebGPU)
        {
            return getRenderDevice().createComputePipeline(rhi::ShaderStageInfo {
                .code       = shader->wgsl,
                .reflection = shader->reflection,
            });
        }

        return getRenderDevice().createComputePipelineBuiltin(*shader);
    }

    rhi::ComputePipeline GeneralGaussianSplatPreprocessPass::createPipeline(const uint64_t variantHash) const
    {
        auto shader = loadGeneralShaderVariant(variantHash, vshadersystem::ShaderStage::eComp);
        if (!shader)
            return {};

        if (getRenderDevice().getBackendApi() == rhi::RenderBackendApi::eWebGPU)
        {
            return getRenderDevice().createComputePipeline(rhi::ShaderStageInfo {
                .code       = shader->wgsl,
                .reflection = shader->reflection,
            });
        }

        return getRenderDevice().createComputePipelineBuiltin(*shader);
    }
} // namespace vultra
