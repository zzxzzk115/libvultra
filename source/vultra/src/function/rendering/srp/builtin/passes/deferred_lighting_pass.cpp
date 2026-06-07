#include "vultra/function/rendering/srp/builtin/passes/deferred_lighting_pass.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/structs/pixel_format.hpp"
#include "vultra/core/rhi/util.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"
#include "vultra/function/rendering/framework/resource_uploader.hpp"
#include "vultra/function/rendering/srp/render_target_desc.hpp"
#include "vultra/function/resource/vtexture_loader.hpp"

#include <texture_headers/ltc_1.dds.bintex.h>
#include <texture_headers/ltc_2.dds.bintex.h>
#include <vasset/vtexture.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <fg/FrameGraph.hpp>
#include <glm/geometric.hpp>

namespace vultra
{
    DeferredLightingPass::DeferredLightingPass() { setShaderProfile(rhi::ShaderProfile::eHighend); }

    namespace
    {
        constexpr auto PASS_NAME = "DeferredLightingPass";
        constexpr uint32_t kMaxDeferredPointLights = 32u;
        constexpr uint32_t kMaxDeferredAreaLights  = 32u;
        constexpr uint32_t kMaxDeferredSpotLights  = 32u;

        struct alignas(16) GpuPointLight
        {
            glm::vec4 posIntensity {0.0f};
            glm::vec4 colorRadius {0.0f};
        };

        struct alignas(16) GpuAreaLight
        {
            glm::vec4 posIntensity {0.0f};
            glm::vec4 uTwoSided {0.0f};
            glm::vec4 vPadding {0.0f};
            glm::vec4 color {0.0f};
        };

        struct alignas(16) GpuSpotLight
        {
            glm::vec4 posIntensity {0.0f};
            glm::vec4 directionRange {0.0f};
            glm::vec4 color {0.0f};
            glm::vec4 coneCosines {0.0f};
        };

        struct alignas(16) GpuLightBlock
        {
            glm::ivec4 counts {0};
            std::array<GpuPointLight, kMaxDeferredPointLights> pointLights {};
            std::array<GpuAreaLight, kMaxDeferredAreaLights> areaLights {};
            std::array<GpuSpotLight, kMaxDeferredSpotLights> spotLights {};
        };

        [[nodiscard]] glm::vec3 safeNormalize(const glm::vec3& v, const glm::vec3& fallback)
        {
            const float len2 = glm::dot(v, v);
            return len2 > 1e-8f ? v * glm::inversesqrt(len2) : fallback;
        }

        [[nodiscard]] GpuLightBlock makeLightBlock(const RenderWorld* renderWorld)
        {
            GpuLightBlock out {};
            if (!renderWorld)
                return out;

            uint32_t pointCount = 0u;
            uint32_t areaCount  = 0u;
            uint32_t spotCount  = 0u;
            for (const auto& light : renderWorld->lights)
            {
                if (light.kind == RenderLightKind::ePoint && pointCount < kMaxDeferredPointLights)
                {
                    auto& dst        = out.pointLights[pointCount++];
                    dst.posIntensity = glm::vec4(light.position, std::max(light.intensity, 0.0f));
                    dst.colorRadius  = glm::vec4(light.color, std::max(light.range, light.radius));
                }
                else if (light.kind == RenderLightKind::eArea && areaCount < kMaxDeferredAreaLights)
                {
                    const glm::vec3 normal = safeNormalize(light.direction, glm::vec3 {0.0f, -1.0f, 0.0f});
                    glm::vec3 up {0.0f, 1.0f, 0.0f};
                    if (std::abs(glm::dot(up, normal)) > 0.95f)
                        up = glm::vec3 {1.0f, 0.0f, 0.0f};
                    const glm::vec3 tangent   = safeNormalize(glm::cross(up, normal), glm::vec3 {1.0f, 0.0f, 0.0f});
                    const glm::vec3 bitangent = safeNormalize(glm::cross(normal, tangent), glm::vec3 {0.0f, 0.0f, 1.0f});

                    auto& dst        = out.areaLights[areaCount++];
                    dst.posIntensity = glm::vec4(light.position, std::max(light.intensity, 0.0f));
                    dst.uTwoSided    = glm::vec4(tangent * (std::max(light.width, 0.001f) * 0.5f),
                                                 light.twoSided ? 1.0f : 0.0f);
                    dst.vPadding     = glm::vec4(bitangent * (std::max(light.height, 0.001f) * 0.5f), 0.0f);
                    dst.color        = glm::vec4(light.color, 0.0f);
                }
                else if (light.kind == RenderLightKind::eSpot && spotCount < kMaxDeferredSpotLights)
                {
                    auto& dst = out.spotLights[spotCount++];
                    dst.posIntensity  = glm::vec4(light.position, std::max(light.intensity, 0.0f));
                    dst.directionRange = glm::vec4(safeNormalize(light.direction, glm::vec3 {0.0f, -1.0f, 0.0f}),
                                                   std::max(light.range, 0.001f));
                    dst.color = glm::vec4(light.color, 0.0f);
                    const float inner = glm::radians(std::max(light.innerConeDegrees, 0.0f));
                    const float outer = glm::radians(std::max(light.outerConeDegrees, light.innerConeDegrees + 0.01f));
                    dst.coneCosines = glm::vec4(std::cos(inner), std::cos(outer), 0.0f, 0.0f);
                }
            }

            out.counts = glm::ivec4(static_cast<int>(pointCount),
                                    static_cast<int>(areaCount),
                                    static_cast<int>(spotCount),
                                    0);
            return out;
        }

    }

    FrameGraphResource DeferredLightingPass::addPass(FrameGraphBuildContext& ctx,
                                                     FrameGraphResource      color,
                                                     FrameGraphResource      normal,
                                                     FrameGraphResource      material,
                                                     FrameGraphResource      emissive,
                                                     FrameGraphResource      depth,
                                                     FrameGraphResource      ssao,
                                                     FrameGraphResource      shadowMap,
                                                     FrameGraphResource      shadowData,
                                                     const ShadowRenderSettings& shadowSettings,
                                                     const PbrLightingSettings& lightingSettings,
                                                     const RenderWorld* renderWorld)
    {
        const auto lightBlockData = makeLightBlock(renderWorld);

        const auto lightBlock = uploadFrameGraphStruct(ctx.fg,
                                                       ctx.frameResources,
                                                       ctx.rd,
                                                       "UploadDeferredLightBlock",
                                                       "DeferredLightBlock",
                                                       framegraph::BufferType::eUniformBuffer,
                                                       lightBlockData);

        struct PassData
        {
            FrameGraphResource camera;
            FrameGraphResource stereoCamera;
            FrameGraphResource lightBlock;
            FrameGraphResource colorIn;
            FrameGraphResource normal;
            FrameGraphResource material;
            FrameGraphResource emissive;
            FrameGraphResource depth;
            FrameGraphResource ssao;
            FrameGraphResource shadowMap;
            FrameGraphResource shadowData;
            FrameGraphResource output;
        };

        const auto colorDesc = makeRenderViewTextureDesc(ctx.view(), rhi::PixelFormat::eRGBA16F);
        auto data = ctx.fg.addCallbackPass<PassData>(
            PASS_NAME,
            [colorDesc,
             color,
             normal,
             material,
             emissive,
             depth,
             ssao,
             shadowMap,
             shadowData,
             lightBlock,
             cameraBlock       = ctx.bb.get<CameraData>().cameraBlock.fgResource,
             stereoCameraBlock = ctx.bb.get<CameraData>().stereoCameraBlock.fgResource,
             useMultiview      = ctx.view().enableMultiview && ctx.view().multiviewCameraCount >= 2u](
                FrameGraph::Builder& builder, PassData& pd) {
                PASS_SETUP_ZONE;

                pd.camera = builder.read(cameraBlock,
                                         framegraph::BindingInfo {
                                             .location      = {.set = 0, .binding = 0},
                                             .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                         });
                if (useMultiview && stereoCameraBlock)
                {
                    pd.stereoCamera = builder.read(stereoCameraBlock,
                                                   framegraph::BindingInfo {
                                                       .location      = {.set = 0, .binding = 23},
                                                       .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                                   });
                }
                pd.colorIn = builder.read(color,
                                          framegraph::TextureRead {
                                              .binding =
                                                  {
                                                      .location      = {.set = 3, .binding = 0},
                                                      .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                                  },
                                              .type        = framegraph::TextureRead::Type::eCombinedImageSampler,
                                              .imageAspect = rhi::ImageAspect::eColor,
                                          });
                pd.normal = builder.read(normal,
                                         framegraph::TextureRead {
                                             .binding =
                                                 {
                                                     .location      = {.set = 3, .binding = 1},
                                                     .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                                 },
                                             .type        = framegraph::TextureRead::Type::eCombinedImageSampler,
                                             .imageAspect = rhi::ImageAspect::eColor,
                                         });
                pd.material = builder.read(material,
                                           framegraph::TextureRead {
                                               .binding =
                                                   {
                                                       .location      = {.set = 3, .binding = 2},
                                                       .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                                   },
                                               .type        = framegraph::TextureRead::Type::eCombinedImageSampler,
                                               .imageAspect = rhi::ImageAspect::eColor,
                                           });
                pd.emissive = builder.read(emissive,
                                           framegraph::TextureRead {
                                               .binding =
                                                   {
                                                       .location      = {.set = 3, .binding = 11},
                                                       .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                                   },
                                               .type        = framegraph::TextureRead::Type::eCombinedImageSampler,
                                               .imageAspect = rhi::ImageAspect::eColor,
                                           });
                pd.depth = builder.read(depth,
                                        framegraph::TextureRead {
                                            .binding =
                                                {
                                                    .location      = {.set = 3, .binding = 3},
                                                    .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                                },
                                            .type        = framegraph::TextureRead::Type::eCombinedImageSampler,
                                            .imageAspect = rhi::ImageAspect::eDepth,
                                        });
                if (ssao)
                {
                    pd.ssao = builder.read(ssao,
                                           framegraph::TextureRead {
                                               .binding =
                                                   {
                                                       .location      = {.set = 3, .binding = 10},
                                                       .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                                   },
                                               .type        = framegraph::TextureRead::Type::eCombinedImageSampler,
                                               .imageAspect = rhi::ImageAspect::eColor,
                                           });
                }
                pd.shadowMap = builder.read(shadowMap,
                                            framegraph::TextureRead {
                                                .binding =
                                                    {
                                                        .location      = {.set = 3, .binding = 4},
                                                        .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                                    },
                                                .type        = framegraph::TextureRead::Type::eCombinedImageSampler,
                                                .imageAspect = rhi::ImageAspect::eDepth,
                                            });
                pd.shadowData = builder.read(shadowData,
                                             framegraph::BindingInfo {
                                                 .location      = {.set = 2, .binding = 0},
                                                 .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                             });
                pd.lightBlock = builder.read(lightBlock,
                                             framegraph::BindingInfo {
                                                 .location      = {.set = 1, .binding = 0},
                                                 .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                             });

                pd.output = builder.create<framegraph::FrameGraphTexture>(
                    "DeferredLightingOutput",
                    colorDesc);
                pd.output = builder.write(pd.output,
                                          framegraph::Attachment {
                                              .index       = 0,
                                              .imageAspect = rhi::ImageAspect::eColor,
                                              .clearValue  = framegraph::ClearValue::eOpaqueBlack,
                                          });
            },
            [this, hasSsao = static_cast<bool>(ssao), shadowSettings, lightingSettings](const PassData&, FrameGraphPassResources&, void* ctxPtr) {
                VULTRA_SCOPED_FRAMEGRAPH_EXEC_CONTEXT(rc, ctxPtr);
                setRenderDevice(rc.rd);
                if (!rc.ext.builtinShaderLib)
                    return;
                setShaderLib(*rc.ext.builtinShaderLib);

                assert(rc.framebufferInfo().has_value());
                const auto framebufferInfo = rc.framebufferInfo().value();
                const auto* pipeline       = getPipeline(rhi::getColorFormat(framebufferInfo, 0),
                                                         framebufferInfo.viewMask);
                if (!pipeline)
                    return;

                RHI_GPU_ZONE(rc.cb, PASS_NAME);
                rc.overrideSampler(rc.resourceSet[3][0], rc.ext.samplers["linear"]);
                rc.overrideSampler(rc.resourceSet[3][1], rc.ext.samplers["nearest"]);
                rc.overrideSampler(rc.resourceSet[3][2], rc.ext.samplers["nearest"]);
                rc.overrideSampler(rc.resourceSet[3][3], rc.ext.samplers["nearest"]);
                rc.overrideSampler(rc.resourceSet[3][11], rc.ext.samplers["nearest"]);
                rc.overrideSampler(rc.resourceSet[3][4], rc.ext.samplers.count("shadow_map") > 0 ?
                                                          rc.ext.samplers["shadow_map"] :
                                                          rc.ext.samplers["nearest"]);
                if (hasSsao)
                    rc.overrideSampler(rc.resourceSet[3][10], rc.ext.samplers["bilinear"]);
                else if (ensureFallbackAoTexture(rc.rd))
                {
                    rhi::prepareForReading(rc.cb, m_FallbackAo);
                    rc.resourceSet[3][10] = rhi::bindings::CombinedImageSampler {
                        .texture = &m_FallbackAo,
                        .sampler = rc.ext.samplers.count("nearest") > 0 ? rc.ext.samplers["nearest"] :
                                                                          rc.ext.samplers["linear"],
                    };
                }
                if (ensureBuiltinLtcTextures(rc.rd))
                {
                    rhi::prepareForReading(rc.cb, m_LtcMat);
                    rhi::prepareForReading(rc.cb, m_LtcMag);
                    rc.resourceSet[3][5] = rhi::bindings::CombinedImageSampler {
                        .texture = &m_LtcMat,
                        .sampler = rc.ext.samplers.count("bilinear") > 0 ? rc.ext.samplers["bilinear"] :
                                                                            rc.ext.samplers["linear"],
                    };
                    rc.resourceSet[3][6] = rhi::bindings::CombinedImageSampler {
                        .texture = &m_LtcMag,
                        .sampler = rc.ext.samplers.count("bilinear") > 0 ? rc.ext.samplers["bilinear"] :
                                                                            rc.ext.samplers["linear"],
                    };
                }
                const bool iblDescriptorsReady = ensureIblTextures(rc.cb, rc.rd, lightingSettings);
                if (iblDescriptorsReady)
                {
                    const bool useEnvironmentIbl = lightingSettings.environmentMap && m_EnvironmentBrdfLut &&
                                                   m_EnvironmentIrradianceMap && m_EnvironmentPrefilteredEnvMap;
                    auto& brdfLut           = useEnvironmentIbl ? m_EnvironmentBrdfLut : m_FallbackBrdfLut;
                    auto& irradianceMap     = useEnvironmentIbl ? m_EnvironmentIrradianceMap : m_FallbackIrradianceMap;
                    auto& prefilteredEnvMap = useEnvironmentIbl ? m_EnvironmentPrefilteredEnvMap :
                                                                 m_FallbackPrefilteredEnvMap;
                    rhi::prepareForReading(rc.cb, brdfLut);
                    rhi::prepareForReading(rc.cb, irradianceMap);
                    rhi::prepareForReading(rc.cb, prefilteredEnvMap);
                    const auto linearSampler = rc.ext.samplers.count("linear") > 0 ? rc.ext.samplers["linear"] :
                                                                                     rc.ext.samplers["bilinear"];
                    rc.resourceSet[3][7] = rhi::bindings::CombinedImageSampler {
                        .texture = &brdfLut,
                        .sampler = linearSampler,
                    };
                    rc.resourceSet[3][8] = rhi::bindings::CombinedImageSampler {
                        .texture = &irradianceMap,
                        .sampler = linearSampler,
                    };
                    rc.resourceSet[3][9] = rhi::bindings::CombinedImageSampler {
                        .texture = &prefilteredEnvMap,
                        .sampler = linearSampler,
                    };
                }
                rc.cb.beginRendering(framebufferInfo).bindPipeline(*pipeline);
                struct LightingPushConstants
                {
                    glm::vec4 directionalLightDirectionShadowStrength {0.0f, -1.0f, 0.0f, 0.85f};
                    glm::vec4 directionalLightColorIntensity {1.0f};
                    glm::vec4 ambientColorIntensity {0.0f};
                    glm::vec4 iblColorIntensity {0.0f};
                    int pcssBlockerSamples {8};
                    int pcssFilterSamples {16};
                    int enableIBL {0};
                    int shadowFilterMode {1};
                    int shadowDebugMode {0};
                    int debugViewMode {0};
                    int pad1 {0};
                } pc {
                    .directionalLightDirectionShadowStrength =
                        glm::vec4(glm::normalize(lightingSettings.directionalLightDirection),
                                  std::clamp(lightingSettings.shadowStrength, 0.0f, 1.0f)),
                    .directionalLightColorIntensity = glm::vec4(lightingSettings.directionalLightColor,
                                                                lightingSettings.directionalLightIntensity),
                    .ambientColorIntensity = glm::vec4(lightingSettings.ambientColor,
                                                       lightingSettings.ambientIntensity),
                    .iblColorIntensity = glm::vec4(lightingSettings.iblColor, lightingSettings.iblIntensity),
                    .pcssBlockerSamples = std::max(shadowSettings.pcssBlockerSamples, 1),
                    .pcssFilterSamples  = std::max(shadowSettings.pcssFilterSamples, 1),
                    .enableIBL = lightingSettings.enableIBL && iblDescriptorsReady ? 1 : 0,
                    .shadowFilterMode = static_cast<int>(shadowSettings.filterMode),
                    .shadowDebugMode = static_cast<int>(shadowSettings.debugMode),
                    .debugViewMode = static_cast<int>(lightingSettings.debugViewMode),
                };
                rc.cb.pushConstants(rhi::ShaderStages::eFragment, 0, &pc);
                rc.bindDescriptorSets(*pipeline);
                rc.cb.drawFullScreenTriangle().endRendering();
            });

        return data.output;
    }

    bool DeferredLightingPass::ensureBuiltinLtcTextures(rhi::RenderDevice& rd)
    {
        if (m_LtcMat && m_LtcMag)
            return true;

        auto makeTexture = [](const std::vector<uint8_t>& data) {
            vasset::VTexture texture {};
            texture.fileFormat = vasset::VTextureFileFormat::eDDS;
            texture.data       = data;
            return texture;
        };

        if (!m_LtcMat)
        {
            auto loaded = resource::loadTextureFromVTexture(makeTexture(ltc_1_dds_bintex), rd);
            if (!loaded)
            {
                VULTRA_CORE_ERROR("[DeferredLightingPass] Failed to load builtin LTC matrix LUT: {}",
                                  loaded.error());
                return false;
            }
            m_LtcMat = std::move(loaded.value());
        }

        if (!m_LtcMag)
        {
            auto loaded = resource::loadTextureFromVTexture(makeTexture(ltc_2_dds_bintex), rd);
            if (!loaded)
            {
                VULTRA_CORE_ERROR("[DeferredLightingPass] Failed to load builtin LTC magnitude LUT: {}",
                                  loaded.error());
                return false;
            }
            m_LtcMag = std::move(loaded.value());
        }

        return m_LtcMat && m_LtcMag;
    }

    bool DeferredLightingPass::ensureIblTextures(rhi::CommandBuffer& cb,
                                                 rhi::RenderDevice&  rd,
                                                 const PbrLightingSettings& lightingSettings)
    {
        if (lightingSettings.environmentMap)
            return ensureEnvironmentIblTextures(cb, rd, lightingSettings);

        return ensureFallbackIblTextures(cb, rd, lightingSettings);
    }

    bool DeferredLightingPass::ensureFallbackIblTextures(rhi::CommandBuffer& cb,
                                                         rhi::RenderDevice&  rd,
                                                         const PbrLightingSettings& lightingSettings)
    {
        (void)cb;
        const glm::vec3 desiredColor = lightingSettings.iblColor * lightingSettings.iblIntensity;
        if (m_FallbackBrdfLut && m_FallbackIrradianceMap && m_FallbackPrefilteredEnvMap &&
            m_FallbackIblColor == desiredColor)
            return true;

        const std::array<float, 4> brdfPixel {1.0f, 0.0f, 0.0f, 1.0f};
        const std::array<float, 4> envPixel {
            std::max(desiredColor.r, 0.0f),
            std::max(desiredColor.g, 0.0f),
            std::max(desiredColor.b, 0.0f),
            1.0f,
        };

        auto uploadTexture2D = [&rd](rhi::Texture& texture, const std::array<float, 4>& pixel) {
            texture = rhi::Texture::Builder {}
                          .setExtent({1u, 1u})
                          .setPixelFormat(rhi::PixelFormat::eRGBA32F)
                          .setNumMipLevels(1u)
                          .setUsageFlags(rhi::ImageUsage::eSampled | rhi::ImageUsage::eTransferDst)
                          .setupOptimalSampler(true)
                          .build(rd);
            if (!texture)
                return false;
            auto staging = rd.createStagingBuffer(sizeof(float) * pixel.size(), pixel.data());
            rhi::upload(rd, staging, {}, texture, false);
            return true;
        };

        auto uploadCubemap = [&rd](rhi::Texture& texture, const std::array<float, 4>& pixel) {
            texture = rhi::Texture::Builder {}
                          .setExtent({1u, 1u})
                          .setPixelFormat(rhi::PixelFormat::eRGBA32F)
                          .setNumMipLevels(1u)
                          .setNumLayers(std::nullopt)
                          .setCubemap(true)
                          .setUsageFlags(rhi::ImageUsage::eSampled | rhi::ImageUsage::eTransferDst)
                          .setupOptimalSampler(true)
                          .build(rd);
            if (!texture)
                return false;

            std::array<std::array<float, 4>, 6> pixels {};
            pixels.fill(pixel);
            auto staging = rd.createStagingBuffer(sizeof(pixels), pixels.data());
            std::array<rhi::BufferImageCopy, 6> copyRegions {};
            for (uint32_t face = 0u; face < 6u; ++face)
            {
                copyRegions[face].bufferOffset      = face * sizeof(float) * pixel.size();
                copyRegions[face].bufferRowLength   = 0;
                copyRegions[face].bufferImageHeight = 0;
                copyRegions[face].aspectMask        = rhi::ImageAspectFlags::eColor;
                copyRegions[face].mipLevel          = 0;
                copyRegions[face].baseArrayLayer    = face;
                copyRegions[face].layerCount        = 1;
                copyRegions[face].imageOffsetX      = 0;
                copyRegions[face].imageOffsetY      = 0;
                copyRegions[face].imageOffsetZ      = 0;
                copyRegions[face].imageExtentWidth  = 1;
                copyRegions[face].imageExtentHeight = 1;
                copyRegions[face].imageExtentDepth  = 1;
            }
            rhi::upload(rd, staging, copyRegions, texture, false);
            return true;
        };

        if (!uploadTexture2D(m_FallbackBrdfLut, brdfPixel) ||
            !uploadCubemap(m_FallbackIrradianceMap, envPixel) ||
            !uploadCubemap(m_FallbackPrefilteredEnvMap, envPixel))
        {
            VULTRA_CORE_ERROR("[DeferredLightingPass] Failed to create fallback IBL textures");
            return false;
        }

        m_FallbackIblColor = desiredColor;
        return true;
    }

    bool DeferredLightingPass::ensureEnvironmentIblTextures(rhi::CommandBuffer& cb,
                                                            rhi::RenderDevice&  rd,
                                                            const PbrLightingSettings& lightingSettings)
    {
        auto* source = lightingSettings.environmentMap;
        if (!source)
            return false;

        if (m_EnvironmentSource == source && m_EnvironmentBrdfLut && m_EnvironmentCubemap &&
            m_EnvironmentIrradianceMap && m_EnvironmentPrefilteredEnvMap)
            return true;

        m_EnvironmentSource              = source;
        m_EnvironmentCubemap             = {};
        m_EnvironmentBrdfLut             = {};
        m_EnvironmentIrradianceMap       = {};
        m_EnvironmentPrefilteredEnvMap   = {};

        if (!m_CubemapConvertPipeline)
            m_CubemapConvertPipeline = createComputePipeline("cubemap_convert.comp");
        if (!m_BrdfPipeline)
            m_BrdfPipeline = createComputePipeline("generate_brdf.comp");
        if (!m_IrradiancePipeline)
            m_IrradiancePipeline = createComputePipeline("generate_irradiance_map.comp");
        if (!m_PrefilterPipeline)
            m_PrefilterPipeline = createComputePipeline("prefilter_envmap.comp");

        if (!m_CubemapConvertPipeline || !m_BrdfPipeline || !m_IrradiancePipeline || !m_PrefilterPipeline)
            return ensureFallbackIblTextures(cb, rd, lightingSettings);

        constexpr uint32_t kCubemapSize = 1024u;
        constexpr uint32_t kIrradianceSize = 64u;
        constexpr uint32_t kBrdfSize = 1024u;
        constexpr uint32_t kPrefilterSize = 1024u;
        constexpr uint32_t kPrefilterMipLevels = 5u;

        m_EnvironmentCubemap = rhi::Texture::Builder {}
                                   .setExtent({kCubemapSize, kCubemapSize})
                                   .setPixelFormat(rhi::PixelFormat::eRGBA16F)
                                   .setNumMipLevels(rhi::calcMipLevels({kCubemapSize, kCubemapSize}))
                                   .setUsageFlags(rhi::ImageUsage::eStorage | rhi::ImageUsage::eSampled |
                                                  rhi::ImageUsage::eTransferSrc | rhi::ImageUsage::eTransferDst)
                                   .setCubemap(true)
                                   .setupOptimalSampler(true)
                                   .build(rd);
        m_EnvironmentBrdfLut = rhi::Texture::Builder {}
                                   .setExtent({kBrdfSize, kBrdfSize})
                                   .setPixelFormat(rhi::PixelFormat::eRGBA16F)
                                   .setNumMipLevels(1u)
                                   .setUsageFlags(rhi::ImageUsage::eStorage | rhi::ImageUsage::eSampled)
                                   .setupOptimalSampler(true)
                                   .build(rd);
        m_EnvironmentIrradianceMap = rhi::Texture::Builder {}
                                         .setExtent({kIrradianceSize, kIrradianceSize})
                                         .setPixelFormat(rhi::PixelFormat::eRGBA16F)
                                         .setNumMipLevels(1u)
                                         .setUsageFlags(rhi::ImageUsage::eStorage | rhi::ImageUsage::eSampled)
                                         .setCubemap(true)
                                         .setupOptimalSampler(true)
                                         .build(rd);
        m_EnvironmentPrefilteredEnvMap = rhi::Texture::Builder {}
                                             .setExtent({kPrefilterSize, kPrefilterSize})
                                             .setPixelFormat(rhi::PixelFormat::eRGBA16F)
                                             .setNumMipLevels(kPrefilterMipLevels)
                                             .setUsageFlags(rhi::ImageUsage::eStorage | rhi::ImageUsage::eSampled)
                                             .setCubemap(true)
                                             .setupOptimalSampler(true)
                                             .build(rd);

        if (!m_EnvironmentCubemap || !m_EnvironmentBrdfLut || !m_EnvironmentIrradianceMap ||
            !m_EnvironmentPrefilteredEnvMap)
        {
            VULTRA_CORE_ERROR("[DeferredLightingPass] Failed to create environment IBL textures");
            return ensureFallbackIblTextures(cb, rd, lightingSettings);
        }

        rhi::prepareForReading(cb, *source);
        rhi::prepareForComputing(cb, m_EnvironmentCubemap);
        {
            const auto descriptors = cb.createDescriptorSetBuilder()
                                         .bind(0,
                                               rhi::bindings::CombinedImageSampler {
                                                   .texture     = source,
                                                   .imageAspect = rhi::ImageAspect::eColor,
                                               })
                                         .bind(1,
                                               rhi::bindings::StorageImage {
                                                   .texture     = &m_EnvironmentCubemap,
                                                   .imageAspect = rhi::ImageAspect::eColor,
                                                   .mipLevel    = 0u,
                                               })
                                         .build(m_CubemapConvertPipeline.getDescriptorSetLayout(0));
            cb.bindPipeline(m_CubemapConvertPipeline)
                .bindDescriptorSet(0, descriptors)
                .dispatch({(kCubemapSize + 7u) / 8u, (kCubemapSize + 7u) / 8u, 6u});
        }
        cb.generateMipmaps(m_EnvironmentCubemap);
        rhi::prepareForReading(cb, m_EnvironmentCubemap);

        rhi::prepareForComputing(cb, m_EnvironmentBrdfLut);
        {
            const auto descriptors = cb.createDescriptorSetBuilder()
                                         .bind(0,
                                               rhi::bindings::StorageImage {
                                                   .texture     = &m_EnvironmentBrdfLut,
                                                   .imageAspect = rhi::ImageAspect::eColor,
                                               })
                                         .build(m_BrdfPipeline.getDescriptorSetLayout(0));
            cb.bindPipeline(m_BrdfPipeline)
                .bindDescriptorSet(0, descriptors)
                .dispatch({(kBrdfSize + 15u) / 16u, (kBrdfSize + 15u) / 16u, 1u});
        }
        rhi::prepareForReading(cb, m_EnvironmentBrdfLut);

        rhi::prepareForComputing(cb, m_EnvironmentIrradianceMap);
        {
            const auto descriptors = cb.createDescriptorSetBuilder()
                                         .bind(0,
                                               rhi::bindings::CombinedImageSampler {
                                                   .texture     = &m_EnvironmentCubemap,
                                                   .imageAspect = rhi::ImageAspect::eColor,
                                               })
                                         .bind(1,
                                               rhi::bindings::StorageImage {
                                                   .texture     = &m_EnvironmentIrradianceMap,
                                                   .imageAspect = rhi::ImageAspect::eColor,
                                               })
                                         .build(m_IrradiancePipeline.getDescriptorSetLayout(0));
            struct PushConstants
            {
                float lodBias {0.0f};
            } pc {};
            cb.bindPipeline(m_IrradiancePipeline)
                .bindDescriptorSet(0, descriptors)
                .pushConstants(rhi::ShaderStages::eCompute, 0, &pc)
                .dispatch({(kIrradianceSize + 7u) / 8u, (kIrradianceSize + 7u) / 8u, 6u});
        }
        rhi::prepareForReading(cb, m_EnvironmentIrradianceMap);

        rhi::prepareForComputing(cb, m_EnvironmentPrefilteredEnvMap);
        for (uint32_t level = 0u; level < kPrefilterMipLevels; ++level)
        {
            const uint32_t mipSize = rhi::calcMipSize(glm::uvec3 {kPrefilterSize, kPrefilterSize, 1u}, level).x;
            const auto descriptors = cb.createDescriptorSetBuilder()
                                         .bind(0,
                                               rhi::bindings::CombinedImageSampler {
                                                   .texture     = &m_EnvironmentCubemap,
                                                   .imageAspect = rhi::ImageAspect::eColor,
                                               })
                                         .bind(1,
                                               rhi::bindings::StorageImage {
                                                   .texture     = &m_EnvironmentPrefilteredEnvMap,
                                                   .imageAspect = rhi::ImageAspect::eColor,
                                                   .mipLevel    = level,
                                               })
                                         .build(m_PrefilterPipeline.getDescriptorSetLayout(0));
            struct PushConstants
            {
                uint32_t mipLevel {0u};
                float    roughness {0.0f};
                uint32_t sampleCount {1024u};
            } pc {
                .mipLevel = level,
                .roughness = static_cast<float>(level) / static_cast<float>(kPrefilterMipLevels - 1u),
                .sampleCount = 1024u,
            };
            cb.bindPipeline(m_PrefilterPipeline)
                .bindDescriptorSet(0, descriptors)
                .pushConstants(rhi::ShaderStages::eCompute, 0, &pc)
                .dispatch({(mipSize + 7u) / 8u, (mipSize + 7u) / 8u, 6u});
        }
        rhi::prepareForReading(cb, m_EnvironmentPrefilteredEnvMap);

        return true;
    }

    bool DeferredLightingPass::ensureFallbackAoTexture(rhi::RenderDevice& rd)
    {
        if (m_FallbackAo)
            return true;

        const std::array<uint8_t, 4> pixel {255u, 255u, 255u, 255u};
        m_FallbackAo = rhi::Texture::Builder {}
                           .setExtent({1u, 1u})
                           .setPixelFormat(rhi::PixelFormat::eRGBA8_UNorm)
                           .setNumMipLevels(1u)
                           .setUsageFlags(rhi::ImageUsage::eSampled | rhi::ImageUsage::eTransferDst)
                           .setupOptimalSampler(true)
                           .build(rd);
        if (!m_FallbackAo)
            return false;

        auto staging = rd.createStagingBuffer(pixel.size(), pixel.data());
        rhi::upload(rd, staging, {}, m_FallbackAo, false);
        return true;
    }

    rhi::GraphicsPipeline DeferredLightingPass::createPipeline(const rhi::PixelFormat colorFormat,
                                                               const uint32_t         viewMask) const
    {
        auto vertexShader = loadHighendShader("fullscreen_triangle.vert", vshadersystem::ShaderStage::eVert);
        rhi::ShaderLibraryRuntime::KeywordValues fragmentKeywords {
            {"USE_MULTIVIEW", viewMask != 0u ? 1u : 0u},
        };
        auto fragmentShader = loadHighendShader("deferred_lighting.frag", vshadersystem::ShaderStage::eFrag, fragmentKeywords);
        if (!vertexShader || !fragmentShader)
        {
            VULTRA_CORE_ERROR("[DeferredLightingPass] Failed to load shaders");
            return {};
        }

        return rhi::GraphicsPipeline::Builder {}
            .setColorFormats({colorFormat})
            .setViewMask(viewMask)
            .setInputAssembly({})
            .addBuiltinShader(rhi::ShaderType::eVertex, *vertexShader)
            .addBuiltinShader(rhi::ShaderType::eFragment, *fragmentShader)
            .setDepthStencil({
                .depthTest  = false,
                .depthWrite = false,
            })
            .setRasterizer({
                .polygonMode = rhi::PolygonMode::eFill,
                .cullMode    = rhi::CullMode::eNone,
            })
            .setBlending(0, {.enabled = false})
            .build(getRenderDevice());
    }

    rhi::ComputePipeline DeferredLightingPass::createComputePipeline(const std::string_view shaderName) const
    {
        auto shader = loadHighendShader(shaderName, vshadersystem::ShaderStage::eComp);
        if (!shader)
        {
            VULTRA_CORE_ERROR("[DeferredLightingPass] Failed to load compute shader {}", shaderName);
            return {};
        }
        return getRenderDevice().createComputePipelineBuiltin(*shader);
    }
} // namespace vultra
