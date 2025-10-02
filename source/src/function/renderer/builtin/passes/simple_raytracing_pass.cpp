#include "vultra/function/renderer/builtin/passes/simple_raytracing_pass.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"
#include "vultra/function/renderer/builtin/framegraph_common.hpp"
#include "vultra/function/renderer/builtin/resources/camera_data.hpp"
#include "vultra/function/renderer/builtin/resources/ibl_data.hpp"
#include "vultra/function/renderer/builtin/resources/light_data.hpp"
#include "vultra/function/renderer/renderer_render_context.hpp"
#include "vultra/function/resource/raw_resource_loader.hpp"

#include <shader_headers/primary_ray.rahit.spv.h>
#include <shader_headers/primary_ray.rchit.spv.h>
#include <shader_headers/primary_ray.rgen.spv.h>
#include <shader_headers/primary_ray.rmiss.spv.h>
#include <shader_headers/shadow_ray.rmiss.spv.h>

#include <texture_headers/ltc_1.dds.bintex.h>
#include <texture_headers/ltc_2.dds.bintex.h>

#include <fg/Blackboard.hpp>
#include <fg/FrameGraph.hpp>

namespace vultra
{
    namespace gfx
    {
        constexpr auto PASS_NAME = "SimpleRaytracingPass";

        SimpleRaytracingPass::SimpleRaytracingPass(rhi::RenderDevice& rd) :
            rhi::RayTracingPass<SimpleRaytracingPass>(rd)
        {
            // Load builtin LTC lookup textures
            auto ltc_1 = resource::loadTextureKTX_DDS_Raw(ltc_1_dds_bintex, rd);
            VULTRA_CORE_ASSERT(ltc_1, "[Builtin-DeferredLightingPass] Failed to load LTC 1 texture");
            m_LTCMat = createRef<vultra::rhi::Texture>(std::move(ltc_1.value()));

            auto ltc_2 = resource::loadTextureKTX_DDS_Raw(ltc_2_dds_bintex, rd);
            VULTRA_CORE_ASSERT(ltc_2, "[Builtin-DeferredLightingPass] Failed to load LTC 2 texture");
            m_LTCMag = createRef<vultra::rhi::Texture>(std::move(ltc_2.value()));
        }

        FrameGraphResource SimpleRaytracingPass::addPass(FrameGraph&            fg,
                                                         FrameGraphBlackboard&  blackboard,
                                                         const rhi::Extent2D&   resolution,
                                                         const RenderableGroup& renderableGroup,
                                                         uint32_t               maxRecursionDepth,
                                                         const glm::vec4&       missColor,
                                                         uint32_t               mode,
                                                         bool                   enableNormalMapping,
                                                         bool                   enableAreaLights,
                                                         bool                   enableIBL,
                                                         float                  exposure,
                                                         ToneMappingMethod      toneMappingMethod)
        {
            // Disable area lights if LUTs missing
            if (enableAreaLights && (!m_LTCMat || !m_LTCMag))
            {
                enableAreaLights = false;
            }

            const auto& iblData = blackboard.get<IBLData>();

            struct SimpleRaytracingData
            {
                FrameGraphResource output;
            };
            const auto& pass = fg.addCallbackPass<SimpleRaytracingData>(
                PASS_NAME,
                [this, &fg, &blackboard, &resolution, &iblData](FrameGraph::Builder&  builder,
                                                                SimpleRaytracingData& data) {
                    PASS_SETUP_ZONE;

                    read(builder, blackboard.get<CameraData>(), framegraph::PipelineStage::eRayTracingShader);
                    read(builder, blackboard.get<LightData>(), framegraph::PipelineStage::eRayTracingShader);

                    // IBL textures
                    builder.read(iblData.brdfLUT,
                                 framegraph::TextureRead {
                                     .binding =
                                         {
                                             .location      = {.set = 3, .binding = 4},
                                             .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                         },
                                     .type        = framegraph::TextureRead::Type::eCombinedImageSampler,
                                     .imageAspect = rhi::ImageAspect::eColor,
                                 });
                    builder.read(iblData.irradianceMap,
                                 framegraph::TextureRead {
                                     .binding =
                                         {
                                             .location      = {.set = 3, .binding = 5},
                                             .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                         },
                                     .type        = framegraph::TextureRead::Type::eCombinedImageSampler,
                                     .imageAspect = rhi::ImageAspect::eColor,
                                 });
                    builder.read(iblData.prefilteredEnvMap,
                                 framegraph::TextureRead {
                                     .binding =
                                         {
                                             .location      = {.set = 3, .binding = 6},
                                             .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                         },
                                     .type        = framegraph::TextureRead::Type::eCombinedImageSampler,
                                     .imageAspect = rhi::ImageAspect::eColor,
                                 });

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
                [this,
                 &renderableGroup,
                 resolution,
                 maxRecursionDepth,
                 &missColor,
                 mode,
                 enableNormalMapping,
                 enableAreaLights,
                 enableIBL,
                 exposure,
                 toneMappingMethod](const SimpleRaytracingData&, auto&, void* ctx) {
                    auto& rc                                    = *static_cast<gfx::RendererRenderContext*>(ctx);
                    auto& [cb, framebufferInfo, sets, samplers] = rc;
                    RHI_GPU_ZONE(cb, PASS_NAME);

                    struct GlobalPushConstants
                    {
                        glm::vec4 missColor;
                        float     exposure;
                        uint32_t  mode;
                        uint32_t  enableNormalMapping;
                        uint32_t  enableAreaLight;
                        uint32_t  enableIBL;
                        uint32_t  toneMappingMethod;
                    };

                    GlobalPushConstants pushConstants {
                        missColor,
                        exposure,
                        mode,
                        enableNormalMapping ? 1u : 0u,
                        enableAreaLights ? 1u : 0u,
                        enableIBL ? 1u : 0u,
                        static_cast<uint32_t>(toneMappingMethod),
                    };

                    auto* pipeline = getPipeline(2); // Hardcoded for now
                    assert(pipeline);

                    // Bind the pipeline, push constants
                    cb.bindPipeline(*pipeline).pushConstants(
                        rhi::ShaderStages::eMiss | rhi::ShaderStages::eClosestHit, 0, &pushConstants);

                    for (const auto& renderable : renderableGroup.renderables)
                    {
                        assert(renderableGroup.tlas);
                        rc.resourceSet[3][0] = rhi::bindings::AccelerationStructureKHR {.as = &renderableGroup.tlas};

                        rc.resourceSet[3][2] = rhi::bindings::CombinedImageSampler {
                            .texture     = m_LTCMat.get(),
                            .imageAspect = rhi::ImageAspect::eColor,
                        };

                        rc.resourceSet[3][3] = rhi::bindings::CombinedImageSampler {
                            .texture     = m_LTCMag.get(),
                            .imageAspect = rhi::ImageAspect::eColor,
                        };

                        rc.resourceSet[2][0] = rhi::bindings::StorageBuffer {
                            .buffer = renderable.mesh->materialBuffer.get(),
                        };
                        rc.resourceSet[2][1] = rhi::bindings::StorageBuffer {
                            .buffer = renderable.mesh->renderMesh.geometryNodeBuffer.get(),
                        };
                        rc.resourceSet[2][2] = rhi::bindings::CombinedImageSamplerArray {
                            .textures    = getRenderDevice().getAllLoadedTextures(),
                            .imageAspect = rhi::ImageAspect::eColor,
                        };

                        assert(samplers.count("bilinear") > 0);
                        rc.overrideSampler(sets[3][4], samplers["bilinear"]); // BRDF LUT
                        rc.overrideSampler(sets[3][5], samplers["bilinear"]); // Irradiance map
                        rc.overrideSampler(sets[3][6], samplers["bilinear"]); // Prefiltered env map

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
                .addBuiltinShader(rhi::ShaderType::eClosestHit, primary_ray_rchit_spv)
                .addBuiltinShader(rhi::ShaderType::eAnyHit, primary_ray_rahit_spv)
                .addRaygenGroup(0)
                .addMissGroup(1)   // miss group 0
                .addMissGroup(2)   // miss group 1 (shadow)
                .addHitGroup(3, 4) // primary closest hit + any hit
                .build(getRenderDevice());
        }
    } // namespace gfx
} // namespace vultra