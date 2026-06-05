#include "vultra/function/rendering/srp/builtin/passes/particle_simulate_pass.hpp"

#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/function/framegraph/framegraph_buffer.hpp"
#include "vultra/function/framegraph/framegraph_import.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/particle/gpu_particle.hpp"

#include <fg/FrameGraph.hpp>

namespace vultra
{
    namespace
    {
        constexpr auto PASS_NAME = "ParticleSimulatePass";
    } // namespace

    ParticleSimulatePass::ParticleSimulatePass() { setShaderProfile(rhi::ShaderProfile::eHighend); }

    std::vector<FrameGraphResource> ParticleSimulatePass::addPass(FrameGraphBuildContext& ctx)
    {
        std::vector<FrameGraphResource> handles;

        auto* gpuSceneView = ctx.view().gpuSceneView;
        if (gpuSceneView == nullptr || gpuSceneView->particleEmitters.empty())
            return handles;

        handles.reserve(gpuSceneView->particleEmitters.size());

        for (const auto& emitter : gpuSceneView->particleEmitters)
        {
            if (emitter.buffer == nullptr || emitter.maxParticles == 0u)
            {
                handles.push_back({});
                continue;
            }

            const auto imported = framegraph::importBuffer(ctx.fg,
                                                           "ParticlePool",
                                                           emitter.buffer,
                                                           framegraph::BufferType::eStorageBuffer,
                                                           sizeof(GpuParticle));

            struct PassData
            {
                FrameGraphResource buffer;
            };

            const auto data = ctx.fg.addCallbackPass<PassData>(
                PASS_NAME,
                [imported](FrameGraph::Builder& builder, PassData& pd) {
                    PASS_SETUP_ZONE;
                    pd.buffer = builder.write(imported,
                                              framegraph::BindingInfo {
                                                  .location      = {.set = 0, .binding = 0},
                                                  .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                              });
                },
                [this, pc = emitter.pc, groups = (emitter.maxParticles + 255u) / 256u](
                    const PassData&, FrameGraphPassResources&, void* ctxPtr) {
                    VULTRA_SCOPED_FRAMEGRAPH_EXEC_CONTEXT(rc, ctxPtr);

                    setRenderDevice(rc.rd);
                    if (!rc.ext.builtinShaderLib)
                        return;
                    setShaderLib(*rc.ext.builtinShaderLibForProfile(getShaderProfile()));

                    RHI_GPU_ZONE(rc.cb, PASS_NAME);

                    const auto variantHash =
                        computeHighendVariantHash("particle_simulate.comp", vshadersystem::ShaderStage::eComp, {});
                    const auto* pipeline = getPipeline(variantHash);
                    if (!pipeline)
                        return;

                    rc.cb.bindPipeline(*pipeline);
                    rc.bindDescriptorSets(*pipeline);
                    rc.cb.pushConstants(rhi::ShaderStages::eCompute, 0, &pc);
                    rc.cb.dispatch({groups, 1u, 1u});
                });

            handles.push_back(data.buffer);
        }

        return handles;
    }

    rhi::ComputePipeline ParticleSimulatePass::createPipeline(uint64_t variantHash) const
    {
        auto shader = loadHighendShaderVariant(variantHash, vshadersystem::ShaderStage::eComp);
        if (!shader)
        {
            VULTRA_CORE_ERROR("[ParticleSimulatePass] Failed to load compute shader variant");
            return {};
        }
        return getRenderDevice().createComputePipelineBuiltin(*shader);
    }
} // namespace vultra
