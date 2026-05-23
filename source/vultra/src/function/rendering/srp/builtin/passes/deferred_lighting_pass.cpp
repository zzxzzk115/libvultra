#include "vultra/function/rendering/srp/builtin/passes/deferred_lighting_pass.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/structs/pixel_format.hpp"
#include "vultra/core/rhi/util.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"
#include "vultra/function/framegraph/transient_buffer.hpp"
#include "vultra/function/framegraph/upload_struct.hpp"
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
                                                     FrameGraphResource      depth,
                                                     FrameGraphResource      shadowMap,
                                                     FrameGraphResource      shadowData,
                                                     const ShadowRenderSettings& shadowSettings,
                                                     const PbrLightingSettings& lightingSettings,
                                                     const RenderWorld* renderWorld)
    {
        const auto lightBlockData = makeLightBlock(renderWorld);

        const auto lightBlock =
            framegraph::uploadStruct(ctx.fg,
                                     "UploadDeferredLightBlock",
                                     framegraph::TransientBuffer<GpuLightBlock> {
                                         .name = "DeferredLightBlock",
                                         .type = framegraph::BufferType::eUniformBuffer,
                                         .data = lightBlockData,
                                     });

        struct PassData
        {
            FrameGraphResource camera;
            FrameGraphResource lightBlock;
            FrameGraphResource colorIn;
            FrameGraphResource normal;
            FrameGraphResource material;
            FrameGraphResource depth;
            FrameGraphResource shadowMap;
            FrameGraphResource shadowData;
            FrameGraphResource output;
        };

        const auto resolution = ctx.view().extent;
        auto data = ctx.fg.addCallbackPass<PassData>(
            PASS_NAME,
            [resolution,
             color,
             normal,
             material,
             depth,
             shadowMap,
             shadowData,
             lightBlock,
             cameraBlock = ctx.bb.get<CameraData>().cameraBlock.fgResource](FrameGraph::Builder& builder,
                                                                            PassData& pd) {
                PASS_SETUP_ZONE;

                pd.camera = builder.read(cameraBlock,
                                         framegraph::BindingInfo {
                                             .location      = {.set = 0, .binding = 0},
                                             .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                         });
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
                    {
                        .extent     = resolution,
                        .format     = rhi::PixelFormat::eRGBA8_UNorm,
                        .usageFlags = rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled |
                                      rhi::ImageUsage::eTransferSrc,
                    });
                pd.output = builder.write(pd.output,
                                          framegraph::Attachment {
                                              .index       = 0,
                                              .imageAspect = rhi::ImageAspect::eColor,
                                              .clearValue  = framegraph::ClearValue::eOpaqueBlack,
                                          });
            },
            [this, shadowSettings, lightingSettings](const PassData&, FrameGraphPassResources&, void* ctxPtr) {
                VULTRA_SCOPED_FRAMEGRAPH_EXEC_CONTEXT(rc, ctxPtr);
                setRenderDevice(rc.rd);
                if (!rc.ext.builtinShaderLib)
                    return;
                setShaderLib(*rc.ext.builtinShaderLib);

                assert(rc.framebufferInfo().has_value());
                const auto framebufferInfo = rc.framebufferInfo().value();
                const auto* pipeline       = getPipeline(rhi::getColorFormat(framebufferInfo, 0));
                if (!pipeline)
                    return;

                RHI_GPU_ZONE(rc.cb, PASS_NAME);
                rc.overrideSampler(rc.resourceSet[3][0], rc.ext.samplers["linear"]);
                rc.overrideSampler(rc.resourceSet[3][1], rc.ext.samplers["nearest"]);
                rc.overrideSampler(rc.resourceSet[3][2], rc.ext.samplers["nearest"]);
                rc.overrideSampler(rc.resourceSet[3][3], rc.ext.samplers["nearest"]);
                rc.overrideSampler(rc.resourceSet[3][4], rc.ext.samplers.count("shadow_map") > 0 ?
                                                          rc.ext.samplers["shadow_map"] :
                                                          rc.ext.samplers["nearest"]);
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
                if (lightingSettings.enableIBL && ensureFallbackIblTextures(rc.rd, lightingSettings))
                {
                    rhi::prepareForReading(rc.cb, m_FallbackBrdfLut);
                    rhi::prepareForReading(rc.cb, m_FallbackIrradianceMap);
                    rhi::prepareForReading(rc.cb, m_FallbackPrefilteredEnvMap);
                    const auto sampler = rc.ext.samplers.count("bilinear") > 0 ? rc.ext.samplers["bilinear"] :
                                                                              rc.ext.samplers["linear"];
                    rc.resourceSet[3][7] = rhi::bindings::CombinedImageSampler {
                        .texture = &m_FallbackBrdfLut,
                        .sampler = sampler,
                    };
                    rc.resourceSet[3][8] = rhi::bindings::CombinedImageSampler {
                        .texture = &m_FallbackIrradianceMap,
                        .sampler = sampler,
                    };
                    rc.resourceSet[3][9] = rhi::bindings::CombinedImageSampler {
                        .texture = &m_FallbackPrefilteredEnvMap,
                        .sampler = sampler,
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
                    int pad0 {0};
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
                    .enableIBL = lightingSettings.enableIBL ? 1 : 0,
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

    bool DeferredLightingPass::ensureFallbackIblTextures(rhi::RenderDevice& rd,
                                                         const PbrLightingSettings& lightingSettings)
    {
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
                          .setNumLayers(1u)
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

    rhi::GraphicsPipeline DeferredLightingPass::createPipeline(const rhi::PixelFormat colorFormat) const
    {
        auto vertexShader = loadHighendShader("fullscreen_triangle.vert", vshadersystem::ShaderStage::eVert);
        auto fragmentShader = loadHighendShader("deferred_lighting.frag", vshadersystem::ShaderStage::eFrag);
        if (!vertexShader || !fragmentShader)
        {
            VULTRA_CORE_ERROR("[DeferredLightingPass] Failed to load shaders");
            return {};
        }

        return rhi::GraphicsPipeline::Builder {}
            .setColorFormats({colorFormat})
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
} // namespace vultra
