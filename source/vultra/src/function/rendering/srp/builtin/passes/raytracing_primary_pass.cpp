#include "vultra/function/rendering/srp/builtin/passes/raytracing_primary_pass.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/structs/render_device_structs.hpp"
#include "vultra/core/rhi/util.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"
#include "vultra/function/rendering/framework/resource_uploader.hpp"
#include "vultra/function/rendering/srp/builtin/resource_keys.hpp"
#include "vultra/function/rendering/render_structs.hpp"
#include "vultra/function/resource/gpu_scene_database.hpp"

#include <fg/FrameGraph.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <glm/geometric.hpp>
#include <glm/vec4.hpp>

namespace vultra
{
    namespace
    {
        constexpr auto PASS_NAME = "RayTracingPrimaryPass";

        struct LegacyRayTracingPushConstants
        {
            glm::vec4 missColor {0.018f, 0.02f, 0.026f, 1.0f};
            float     exposure {1.0f};
            uint32_t  mode {11u};
            uint32_t  enableNormalMapping {1u};
            uint32_t  enableAreaLights {0u};
            uint32_t  enableIBL {0u};
            uint32_t  toneMappingMethod {0u};
        };

        constexpr uint32_t kMaxRtPointLights = 32u;

        struct alignas(16) RtDirectionalLight
        {
            glm::vec4 directionShadowStrength {-0.35f, -0.8f, -0.25f, 1.0f};
            glm::vec4 colorIntensity {1.0f, 0.96f, 0.9f, 8.0f};
        };

        struct alignas(16) RtPointLight
        {
            glm::vec4 posIntensity {0.0f};
            glm::vec4 colorRadius {0.0f};
        };

        struct alignas(16) RtLightBlock
        {
            glm::uvec4 counts {0u};
            RtDirectionalLight directional {};
            std::array<RtPointLight, kMaxRtPointLights> pointLights {};
        };

        [[nodiscard]] glm::vec3 safeNormalize(const glm::vec3& v, const glm::vec3& fallback)
        {
            const float len2 = glm::dot(v, v);
            return len2 > 1e-8f ? v * glm::inversesqrt(len2) : fallback;
        }

        [[nodiscard]] RtLightBlock makeRtLightBlock(const RenderWorld* renderWorld)
        {
            RtLightBlock out {};
            if (!renderWorld)
                return out;

            bool     hasDirectional = false;
            uint32_t pointCount     = 0u;
            for (const auto& light : renderWorld->lights)
            {
                if (!light.castsShadow)
                    continue;

                if (light.kind == RenderLightKind::eDirectional && !hasDirectional)
                {
                    hasDirectional = true;
                    out.directional.directionShadowStrength =
                        glm::vec4(safeNormalize(light.direction, glm::vec3 {-0.35f, -0.8f, -0.25f}), 1.0f);
                    out.directional.colorIntensity = glm::vec4(light.color, std::max(light.intensity, 0.0f));
                }
                else if (light.kind == RenderLightKind::ePoint && pointCount < kMaxRtPointLights)
                {
                    auto& dst        = out.pointLights[pointCount++];
                    dst.posIntensity = glm::vec4(light.position, std::max(light.intensity, 0.0f));
                    dst.colorRadius  = glm::vec4(light.color, std::max(light.range, light.radius));
                }
            }

            out.counts = glm::uvec4(hasDirectional ? 1u : 0u, pointCount, 0u, 0u);
            return out;
        }
    } // namespace

    RayTracingPrimaryPass::RayTracingPrimaryPass() { setShaderProfile(rhi::ShaderProfile::eHighend); }

    FrameGraphResource RayTracingPrimaryPass::addPass(FrameGraphBuildContext& ctx)
    {
        struct PassData
        {
            FrameGraphResource camera;
            FrameGraphResource lightBlock;
            FrameGraphResource color;
        };

        const auto lightBlock = uploadFrameGraphStruct(ctx.fg,
                                                       ctx.frameResources,
                                                       ctx.rd,
                                                       "UploadRayTracingLightBlock",
                                                       "RayTracingLightBlock",
                                                       framegraph::BufferType::eUniformBuffer,
                                                       makeRtLightBlock(ctx.view().renderWorld));

        const auto output = ctx.fg.addCallbackPass<PassData>(
            PASS_NAME,
            [&ctx, lightBlock, cameraBlock = ctx.bb.get<CameraData>().cameraBlock.fgResource](
                FrameGraph::Builder& builder,
                PassData&            data) {
                PASS_SETUP_ZONE;

                data.camera = builder.read(cameraBlock,
                                           framegraph::BindingInfo {
                                               .location      = {.set = 1, .binding = 0},
                                               .pipelineStage = framegraph::PipelineStage::eRayTracingShader,
                                           });
                data.lightBlock = builder.read(lightBlock,
                                               framegraph::BindingInfo {
                                                   .location      = {.set = 1, .binding = 1},
                                                   .pipelineStage = framegraph::PipelineStage::eRayTracingShader,
                                               });
                data.color = builder.create<framegraph::FrameGraphTexture>(
                    "RayTracingPrimary Color",
                    {
                        .extent     = ctx.view().extent,
                        .format     = rhi::PixelFormat::eRGBA16F,
                        .usageFlags = rhi::ImageUsage::eStorage | rhi::ImageUsage::eSampled |
                                      rhi::ImageUsage::eTransferSrc | rhi::ImageUsage::eTransferDst,
                    });
                data.color = builder.write(data.color,
                                           framegraph::ImageWrite {
                                               .binding =
                                                   {
                                                       .location      = {.set = 3, .binding = 1},
                                                       .pipelineStage = framegraph::PipelineStage::eRayTracingShader,
                                                   },
                                               .imageAspect = rhi::ImageAspect::eColor,
                                           });
            },
            [this](const PassData& data, FrameGraphPassResources& resources, void* ctxPtr) {
                VULTRA_SCOPED_FRAMEGRAPH_EXEC_CONTEXT(rc, ctxPtr);

                const auto* camera = rc.view().camera;
                const auto  clearColor = camera ? camera->clearValue : glm::vec4 {0.018f, 0.02f, 0.026f, 1.0f};
                auto* gpuSceneDatabase = rc.view().gpuSceneDatabase;
                auto* output = resources.get<framegraph::FrameGraphTexture>(data.color).texture;

                if (!gpuSceneDatabase || !gpuSceneDatabase->rayTracingTlas ||
                    !gpuSceneDatabase->rayTracingInstanceBuffer || !gpuSceneDatabase->rayTracingGeometryNodeBuffer ||
                    !gpuSceneDatabase->resources || !gpuSceneDatabase->resources->materialTableBuffer ||
                    !gpuSceneDatabase->resources->materialParams.gpu)
                {
                    if (output)
                        rhi::clearImageForComputing(rc.cb, *output, clearColor);
                    return;
                }

                setRenderDevice(rc.rd);
                setShaderLib(*rc.ext.builtinShaderLibForProfile(getShaderProfile()));
                auto* pipeline = getPipeline();
                if (!pipeline)
                {
                    if (output)
                        rhi::clearImageForComputing(rc.cb, *output, clearColor);
                    return;
                }

                const LegacyRayTracingPushConstants pc {
                    .missColor           = camera ? camera->clearValue : clearColor,
                    .exposure            = 1.0f,
                    .mode                = 11u,
                    .enableNormalMapping = 1u,
                    .enableAreaLights    = 0u,
                    .enableIBL           = 0u,
                    .toneMappingMethod   = 0u,
                };

                auto materialTextures = gpuSceneDatabase->resources->getBindlessTextureHandles();
                const rhi::Texture* fallbackTexture = !materialTextures.empty() ? materialTextures[0] : nullptr;
                if (!fallbackTexture)
                {
                    if (output)
                        rhi::clearImageForComputing(rc.cb, *output, clearColor);
                    return;
                }
                for (auto*& texture : materialTextures)
                {
                    if (!texture)
                        texture = fallbackTexture;
                }

                rc.resourceSet[3][0] =
                    rhi::bindings::AccelerationStructureKHR {.as = &gpuSceneDatabase->rayTracingTlas};
                rc.resourceSet[3][4] = rhi::bindings::CombinedImageSamplerArray {
                    .textures    = materialTextures,
                    .imageAspect = rhi::ImageAspect::eColor,
                };

                rc.resourceSet[2][0] =
                    rhi::bindings::StorageBuffer {.buffer = gpuSceneDatabase->rayTracingInstanceBuffer.get()};
                rc.resourceSet[2][1] =
                    rhi::bindings::StorageBuffer {.buffer = gpuSceneDatabase->resources->materialTableBuffer.get()};
                rc.resourceSet[2][2] =
                    rhi::bindings::StorageBuffer {.buffer = gpuSceneDatabase->rayTracingGeometryNodeBuffer.get()};
                rc.resourceSet[2][4] =
                    rhi::bindings::StorageBuffer {.buffer = gpuSceneDatabase->resources->materialParams.gpu.get()};

                RHI_GPU_ZONE(rc.cb, PASS_NAME);
                if (output)
                    rhi::prepareForRaytracing(rc.cb, *output);
                rc.cb.bindPipeline(*pipeline);
                rc.bindDescriptorSets(*pipeline);
                rc.cb.pushConstants(rhi::ShaderStages::eMiss, 0, &pc);

                Ref<rhi::ShaderBindingTable> sbt;
                try
                {
                    sbt = pipeline->getSBT(rc.rd);
                }
                catch (const std::exception& e)
                {
                    VULTRA_CORE_ERROR("[RayTracingPrimaryPass] Failed to create shader binding table: {}", e.what());
                    if (output)
                        rhi::clearImageForComputing(rc.cb, *output, clearColor);
                    return;
                }
                if (!sbt)
                {
                    if (output)
                        rhi::clearImageForComputing(rc.cb, *output, clearColor);
                    return;
                }

                rc.cb.traceRays(*sbt, {rc.view().extent.width, rc.view().extent.height, 1u});
                if (output)
                    rhi::prepareForReading(rc.cb, *output);
            });

        ctx.data.set(kResKey_FinalCompositionSource, output.color);
        return output.color;
    }

    rhi::RayTracingPipeline RayTracingPrimaryPass::createPipeline()
    {
        auto& rd = getRenderDevice();

        if (!HasFlagValues(rd.getFeatureFlag(), rhi::RenderDeviceFeatureFlagBits::eRayTracingPipeline))
        {
            VULTRA_CORE_WARN("[RayTracingPrimaryPass] Ray tracing pipeline feature is unavailable");
            return {};
        }

        auto raygen     = loadHighendShader("default_rt_primary.rgen", vshadersystem::ShaderStage::eRgen);
        auto miss       = loadHighendShader("primary_ray.rmiss", vshadersystem::ShaderStage::eRmiss);
        auto shadowMiss = loadHighendShader("shadow_ray.rmiss", vshadersystem::ShaderStage::eRmiss);
        auto closestHit = loadHighendShader("default_rt_primary.rchit", vshadersystem::ShaderStage::eRchit);
        auto anyHit     = loadHighendShader("default_rt_primary.rahit", vshadersystem::ShaderStage::eRahit);
        if (!raygen || !miss || !shadowMiss || !closestHit || !anyHit)
        {
            VULTRA_CORE_ERROR("[RayTracingPrimaryPass] Missing ray tracing shader variant in builtin_highend.vshlib");
            return {};
        }

        try
        {
            return rhi::RayTracingPipeline::Builder {}
                .setMaxRecursionDepth(2)
                .addBuiltinShader(rhi::ShaderType::eRayGen, raygen->spirv)
                .addBuiltinShader(rhi::ShaderType::eMiss, miss->spirv)
                .addBuiltinShader(rhi::ShaderType::eMiss, shadowMiss->spirv)
                .addBuiltinShader(rhi::ShaderType::eClosestHit, closestHit->spirv)
                .addBuiltinShader(rhi::ShaderType::eAnyHit, anyHit->spirv)
                .addRaygenGroup(0)
                .addMissGroup(1)
                .addMissGroup(2)
                .addHitGroup(3, 4)
                .build(rd);
        }
        catch (const std::exception& e)
        {
            VULTRA_CORE_ERROR("[RayTracingPrimaryPass] Failed to create ray tracing pipeline: {}", e.what());
            return {};
        }
    }

} // namespace vultra
