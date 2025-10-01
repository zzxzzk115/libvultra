#include "vultra/function/renderer/builtin/passes/simple_raytracing_pass.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"
#include "vultra/function/renderer/builtin/framegraph_common.hpp"
#include "vultra/function/renderer/builtin/resources/camera_data.hpp"
#include "vultra/function/renderer/builtin/resources/light_data.hpp"
#include "vultra/function/renderer/renderer_render_context.hpp"

#include <shader_headers/primary_closest_hit.rchit.spv.h>
#include <shader_headers/primary_ray.rgen.spv.h>
#include <shader_headers/primary_ray.rmiss.spv.h>
#include <shader_headers/shadow_ray.rmiss.spv.h>

#include <fg/Blackboard.hpp>
#include <fg/FrameGraph.hpp>

namespace vultra
{
    namespace gfx
    {
        constexpr auto PASS_NAME = "SimpleRaytracingPass";

        SimpleRaytracingPass::SimpleRaytracingPass(rhi::RenderDevice& rd) :
            rhi::RayTracingPass<SimpleRaytracingPass>(rd)
        {}

        FrameGraphResource SimpleRaytracingPass::addPass(FrameGraph&                 fg,
                                                         FrameGraphBlackboard&       blackboard,
                                                         const rhi::Extent2D&        resolution,
                                                         const RenderPrimitiveGroup& renderPrimitiveGroup,
                                                         uint32_t                    maxRecursionDepth,
                                                         const glm::vec4&            missColor)
        {
            struct SimpleRaytracingData
            {
                FrameGraphResource output;
            };
            const auto& pass = fg.addCallbackPass<SimpleRaytracingData>(
                PASS_NAME,
                [this, &fg, &blackboard, &resolution](FrameGraph::Builder& builder, SimpleRaytracingData& data) {
                    PASS_SETUP_ZONE;

                    read(builder, blackboard.get<CameraData>());
                    // read(builder, blackboard.get<LightData>());

                    data.output = builder.create<framegraph::FrameGraphTexture>(
                        "SimpleRaytracingPass - Output",
                        {
                            .extent     = resolution,
                            .format     = rhi::PixelFormat::eRGBA8_UNorm,
                            .usageFlags = rhi::ImageUsage::eStorage | rhi::ImageUsage::eSampled,
                        });
                    data.output =
                        builder.write(data.output,
                                      framegraph::ImageWrite {
                                          .binding =
                                              {
                                                  .location      = {.set = 3, .binding = 1},
                                                  .pipelineStage = framegraph::PipelineStage::eRayTracingShader,
                                              },
                                          .imageAspect = rhi::ImageAspect::eColor,
                                      });
                },
                [this, renderPrimitiveGroup, resolution, maxRecursionDepth, &missColor](
                    const SimpleRaytracingData&, auto&, void* ctx) {
                    auto& rc                                    = *static_cast<gfx::RendererRenderContext*>(ctx);
                    auto& [cb, framebufferInfo, sets, samplers] = rc;
                    RHI_GPU_ZONE(cb, PASS_NAME);

                    struct GlobalPushConstants
                    {
                        glm::vec4 m;
                    };

                    GlobalPushConstants pushConstants {
                        missColor,
                    };

                    auto* pipeline = getPipeline(2); // Hardcoded for now
                    assert(pipeline);

                    // Bind the pipeline, push constants
                    cb.bindPipeline(*pipeline).pushConstants(
                        rhi::ShaderStages::eMiss | rhi::ShaderStages::eClosestHit, 0, &pushConstants);

                    const auto& [opaquePrimitives, alphaMaskingPrimitives, decalPrimitives] = renderPrimitiveGroup;

                    for (const auto& opaquePrimitive : opaquePrimitives)
                    {
                        assert(opaquePrimitive.mesh->renderMesh.tlas);
                        rc.resourceSet[3][0] =
                            rhi::bindings::AccelerationStructureKHR {.as = &opaquePrimitive.mesh->renderMesh.tlas};

                        rc.resourceSet[2][0] = rhi::bindings::StorageBuffer {
                            .buffer = opaquePrimitive.mesh->renderMesh.materialBuffer.get(),
                        };
                        rc.resourceSet[2][1] = rhi::bindings::StorageBuffer {
                            .buffer = opaquePrimitive.mesh->renderMesh.geometryNodeBuffer.get(),
                        };
                        rc.resourceSet[2][2] = rhi::bindings::CombinedImageSamplerArray {
                            .textures    = getRenderDevice().getAllLoadedTextures(),
                            .imageAspect = rhi::ImageAspect::eColor,
                        };

                        rc.bindDescriptorSets(*pipeline);

                        // Trace rays
                        cb.traceRays(*pipeline->getSBT(getRenderDevice()),
                                     {
                                         static_cast<float>(resolution.width),
                                         static_cast<float>(resolution.height),
                                         1.0f,
                                     });
                    }

                    rc.endRayTracing();
                });

            return pass.output;
        }

        rhi::RayTracingPipeline SimpleRaytracingPass::createPipeline(uint32_t maxRecursionDepth) const
        {
            return rhi::RayTracingPipeline::Builder {}
                .setMaxRecursionDepth(maxRecursionDepth)
                .addBuiltinShader(rhi::ShaderType::eRayGen, primary_ray_rgen_spv)
                .addBuiltinShader(rhi::ShaderType::eMiss, primary_ray_rmiss_spv)
                .addBuiltinShader(rhi::ShaderType::eMiss, shadow_ray_rmiss_spv)
                .addBuiltinShader(rhi::ShaderType::eClosestHit, primary_closest_hit_rchit_spv)
                .addRaygenGroup(0)
                .addMissGroup(1) // miss group 0
                .addMissGroup(2) // miss group 1 (shadow)
                .addHitGroup(3)  // only primary hit group
                .build(getRenderDevice());
        }
    } // namespace gfx
} // namespace vultra