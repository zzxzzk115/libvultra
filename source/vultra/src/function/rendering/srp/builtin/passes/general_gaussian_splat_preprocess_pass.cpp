#include "vultra/function/rendering/srp/builtin/passes/general_gaussian_splat_preprocess_pass.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/function/framegraph/framegraph_buffer.hpp"
#include "vultra/function/framegraph/framegraph_import.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/rendering/srp/builtin/resource_keys.hpp"

#include <fg/FrameGraph.hpp>

#include <algorithm>
#include <initializer_list>
#include <vector>

namespace vultra
{
    namespace
    {
        constexpr auto PASS_NAME = "GeneralGaussianSplatPreprocessPass";
        constexpr auto kStereoCameraBinding = 23u;

        struct GeneralGaussianSplatPreprocessPushConstants
        {
            uint32_t pointCount {0};
            uint32_t maxVisibleSplats {0};
            uint32_t padding0 {0};
            uint32_t padding1 {0};
        };

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

        if (gpuSceneView->generalGaussianSplatDispatchArgsBuffer)
        {
            ctx.data.set(kResKey_GeneralGaussianSplatDispatchArgsBuffer,
                         framegraph::importBuffer(ctx.fg,
                                                  "GeneralGaussianSplatDispatchArgsBuffer",
                                                  gpuSceneView->generalGaussianSplatDispatchArgsBuffer.get(),
                                                  framegraph::BufferType::eDispatchIndirectBuffer,
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
        auto dispatchArgsBuffer = ctx.data.tryGet(kResKey_GeneralGaussianSplatDispatchArgsBuffer);
        auto indirectBuffer     = ctx.data.tryGet(kResKey_GeneralGaussianSplatIndirectBuffer);
        auto sortStorageBuffer  = ctx.data.tryGet(kResKey_GeneralGaussianSplatSortStorageBuffer);
        auto shBuffer           = ctx.data.tryGet(kResKey_GeneralGaussianSplatShBuffer);

        const uint32_t pointCount = gpuSceneView->activeGeneralGaussianSplatPoints;
        const uint32_t maxVisible = gpuSceneView->maxGeneralGaussianSplatVisibleSplats;

        if (!cameraBlock || !drawBuffer || !packedSourceBuffer || !visibleSplatBuffer || !sortKeyBuffer ||
            !sortIndexBuffer || !visibleCountBuffer || !indirectBuffer || !sortStorageBuffer || !shBuffer ||
            (!useDirectPrefix && !selectedSourceBuffer))
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
            FrameGraphResource dispatchArgsBuffer;
            FrameGraphResource indirectBuffer;
            FrameGraphResource sortStorageBuffer;
            FrameGraphResource shBuffer;
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
             dispatchArgsBuffer,
             indirectBuffer,
             sortStorageBuffer,
             shBuffer,
             selectedSourceBuffer,
             useDirectPrefix](FrameGraph::Builder& builder, PassData& data) {
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
                data.visibleSplatBuffer = builder.write(visibleSplatBuffer,
                                                        framegraph::BindingInfo {
                                                            .location      = {.set = 0, .binding = 15},
                                                            .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                                        });
                data.sortKeyBuffer      = builder.write(sortKeyBuffer,
                                                   framegraph::BindingInfo {
                                                            .location      = {.set = 0, .binding = 16},
                                                            .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                                   });
                data.sortIndexBuffer    = builder.write(sortIndexBuffer,
                                                     framegraph::BindingInfo {
                                                            .location      = {.set = 0, .binding = 17},
                                                            .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                                     });
                data.visibleCountBuffer = builder.write(visibleCountBuffer,
                                                        framegraph::BindingInfo {
                                                            .location      = {.set = 0, .binding = 18},
                                                            .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                                        });
                if (dispatchArgsBuffer)
                {
                    data.dispatchArgsBuffer =
                        builder.write(dispatchArgsBuffer,
                                      framegraph::BindingInfo {
                                          .location      = {.set = 0, .binding = 19},
                                          .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                      });
                }
                data.indirectBuffer    = builder.write(indirectBuffer,
                                                    framegraph::BindingInfo {
                                                           .location      = {.set = 0, .binding = 20},
                                                           .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                                    });
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
            [this, pointCount, maxVisible, useMultiview, useDirectPrefix](
                const PassData& data, FrameGraphPassResources& resources, void* ctxPtr) {
                VULTRA_SCOPED_FRAMEGRAPH_EXEC_CONTEXT(rc, ctxPtr);
                setRenderDevice(rc.rd);
                if (!rc.ext.builtinShaderLib)
                {
                    return;
                }
                setShaderLib(*rc.ext.builtinShaderLib);

                auto bindSubset = [&rc](const rhi::BasePipeline& pipeline, const auto& bindings) {
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

                    rc.bindDescriptorSets(pipeline);
                    rc.resourceSet = std::move(saved);
                };

                RHI_GPU_ZONE(rc.cb, PASS_NAME);

                auto* gpuSceneView = rc.view().gpuSceneView;
                if (!gpuSceneView || pointCount == 0u || maxVisible == 0u)
                {
                    return;
                }

                auto* visibleCountBuf = resources.get<framegraph::FrameGraphBuffer>(data.visibleCountBuffer).buffer;
                auto* dispatchArgsBuf =
                    data.dispatchArgsBuffer ?
                        resources.get<framegraph::FrameGraphBuffer>(data.dispatchArgsBuffer).buffer :
                        nullptr;

                {
                    RHI_GPU_ZONE(rc.cb, "GeneralGaussianSplatPreprocess::ProjectCull");

                    if (visibleCountBuf)
                    {
                        const uint32_t zero = 0u;
                        rc.cb.update(*visibleCountBuf, 0u, sizeof(uint32_t), &zero);
                    }
                    if (dispatchArgsBuf)
                    {
                        const uint32_t zeroArgs[4] = {0u, 1u, 1u, 0u};
                        rc.cb.update(*dispatchArgsBuf, 0u, sizeof(zeroArgs), zeroArgs);
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

                    const auto* preprocessPipeline = getPipeline(useMultiview, useDirectPrefix);
                    if (!preprocessPipeline)
                    {
                        return;
                    }

                    GeneralGaussianSplatPreprocessPushConstants pc {};
                    pc.pointCount       = pointCount;
                    pc.maxVisibleSplats = maxVisible;

                    rc.cb.bindPipeline(*preprocessPipeline);
                    std::vector<uint32_t> preprocessBindings {0u, 13u, 14u, 15u, 16u, 17u, 18u, 19u, 22u};
                    if (!useDirectPrefix)
                    {
                        preprocessBindings.push_back(27u);
                    }
                    if (useMultiview)
                    {
                        preprocessBindings.push_back(kStereoCameraBinding);
                    }
                    bindSubset(*preprocessPipeline, preprocessBindings);
                    rc.cb.pushConstants(rhi::ShaderStages::eCompute, 0, &pc);
                    const uint32_t dispatchX = (pointCount + 255u) / 256u;
                    rc.cb.dispatch({dispatchX, 1u, 1u});
                    rc.cb.insertComputeUavBarrier();
                }

                auto* sortKeyBuf     = resources.get<framegraph::FrameGraphBuffer>(data.sortKeyBuffer).buffer;
                auto* sortIndexBuf   = resources.get<framegraph::FrameGraphBuffer>(data.sortIndexBuffer).buffer;
                auto* sortStorageBuf = resources.get<framegraph::FrameGraphBuffer>(data.sortStorageBuffer).buffer;

                if (gpuSceneView->generalGaussianSplatSorter.has_value() &&
                    static_cast<bool>(*gpuSceneView->generalGaussianSplatSorter) && visibleCountBuf && sortKeyBuf &&
                    sortIndexBuf && sortStorageBuf)
                {
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

                    auto writeIndirectVariantHash = computeShaderVariantHash(
                        "gaussian_splat_write_indirect.comp", vshadersystem::ShaderStage::eComp, {});
                    const auto* writeIndirectPipeline = getPipeline(writeIndirectVariantHash);
                    if (!writeIndirectPipeline)
                    {
                        return;
                    }

                    rc.cb.bindPipeline(*writeIndirectPipeline);
                    bindSubset(*writeIndirectPipeline, std::initializer_list<uint32_t> {18u, 20u});
                    rc.cb.dispatch({1u, 1u, 1u});
                }
            });
    }

    rhi::ComputePipeline GeneralGaussianSplatPreprocessPass::createPipeline(const bool useMultiview,
                                                                            const bool useDirectPrefix) const
    {
        rhi::ShaderLibraryRuntime::KeywordValues keywords {
            {"USE_MULTIVIEW", useMultiview ? 1u : 0u},
            {"USE_DIRECT_PREFIX", useDirectPrefix ? 1u : 0u},
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

        return getRenderDevice().createComputePipelineBuiltin(shader->spirv);
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

        return getRenderDevice().createComputePipelineBuiltin(shader->spirv);
    }
} // namespace vultra
