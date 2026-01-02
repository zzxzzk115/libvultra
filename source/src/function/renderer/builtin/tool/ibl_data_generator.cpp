#include "vultra/function/renderer/builtin/tool/ibl_data_generator.hpp"
#include "vultra/core/rhi/command_buffer.hpp"

#include <shader_headers/generate_brdf.comp.spv.h>
#include <shader_headers/generate_irradiance_map.comp.spv.h>
#include <shader_headers/prefilter_envmap.comp.spv.h>

#include <glm/ext/scalar_constants.hpp>
#include <glm/glm.hpp>

namespace vultra
{
    namespace gfx
    {
        IBLDataGenerator::IBLDataGenerator(rhi::RenderDevice& rd) : m_RenderDevice(rd)
        {
            m_BrdfPipeline            = createBrdfPipeline();
            m_IrradiancePipeline      = createIrradiancePipeline();
            m_PrefilterEnvMapPipeline = createPrefilterEnvMapPipeline();
        }

        Ref<rhi::Texture> IBLDataGenerator::generateBrdfLUT(rhi::CommandBuffer& cb)
        {
            constexpr auto kSize = 1024u;

            auto brdf = createRef<rhi::Texture>(
                std::move(rhi::Texture::Builder {}
                              .setExtent({kSize, kSize})
                              .setPixelFormat(rhi::PixelFormat::eRGBA16F)
                              .setNumMipLevels(1)
                              .setUsageFlags(rhi::ImageUsage::eStorage | rhi::ImageUsage::eSampled)
                              .build(m_RenderDevice)));

            rhi::prepareForComputing(cb, *brdf);

            auto descriptors =
                cb.createDescriptorSetBuilder()
                    .bind(0,
                          rhi::bindings::StorageImage {.texture = brdf.get(), .imageAspect = rhi::ImageAspect::eColor})
                    .build(m_BrdfPipeline.getDescriptorSetLayout(0));

            {
                // RHI_GPU_ZONE(cb, "BRDF LUT Generation");
                cb.bindPipeline(m_BrdfPipeline)
                    .bindDescriptorSet(0, descriptors)
                    .dispatch({(kSize + 15) / 16, (kSize + 15) / 16, 1});
            }

            rhi::prepareForReading(cb, *brdf);
            m_BrdfLUTGenerated = true;
            return brdf;
        }

        Ref<rhi::Texture> IBLDataGenerator::generateIrradianceMap(rhi::CommandBuffer& cb, rhi::Texture& cubemap)
        {
            constexpr auto kSize = 64u;

            auto irradiance = createRef<rhi::Texture>(
                std::move(rhi::Texture::Builder {}
                              .setExtent({kSize, kSize})
                              .setPixelFormat(rhi::PixelFormat::eRGBA16F)
                              .setNumMipLevels(1)
                              .setUsageFlags(rhi::ImageUsage::eStorage | rhi::ImageUsage::eSampled)
                              .setCubemap(true)
                              .build(m_RenderDevice)));

            m_RenderDevice.setupSampler(*irradiance,
                                        {
                                            .magFilter    = rhi::TexelFilter::eLinear,
                                            .minFilter    = rhi::TexelFilter::eLinear,
                                            .mipmapMode   = rhi::MipmapMode::eNearest,
                                            .addressModeS = rhi::SamplerAddressMode::eClampToEdge,
                                            .addressModeT = rhi::SamplerAddressMode::eClampToEdge,
                                            .addressModeR = rhi::SamplerAddressMode::eClampToEdge,
                                        });

            rhi::prepareForComputing(cb, *irradiance);

            constexpr auto kDescriptorSetId = 0;

            auto descriptors = cb.createDescriptorSetBuilder()
                                   .bind(0,
                                         rhi::bindings::CombinedImageSampler {.texture     = &cubemap,
                                                                              .imageAspect = rhi::ImageAspect::eColor})
                                   .bind(1,
                                         rhi::bindings::StorageImage {.texture     = irradiance.get(),
                                                                      .imageAspect = rhi::ImageAspect::eColor})
                                   .build(m_IrradiancePipeline.getDescriptorSetLayout(kDescriptorSetId));

            struct PushConstants
            {
                float lodBias; // matches shader
            } pc {};

            pc.lodBias = 0.0f;

            cb.bindPipeline(m_IrradiancePipeline)
                .bindDescriptorSet(kDescriptorSetId, descriptors)
                .pushConstants(rhi::ShaderStages::eCompute, 0, &pc)
                .dispatch({(kSize + 7) / 8, (kSize + 7) / 8, 1});

            rhi::prepareForReading(cb, *irradiance);
            return irradiance;
        }

        Ref<rhi::Texture> IBLDataGenerator::generatePrefilterEnvMap(rhi::CommandBuffer& cb, rhi::Texture& cubemap)
        {
            constexpr auto kSize      = 1024u;
            constexpr auto kMipLevels = 5u;

            auto prefilteredEnvMap = createRef<rhi::Texture>(
                std::move(rhi::Texture::Builder {}
                              .setExtent({kSize, kSize})
                              .setPixelFormat(rhi::PixelFormat::eRGBA16F)
                              .setNumMipLevels(kMipLevels)
                              .setUsageFlags(rhi::ImageUsage::eStorage | rhi::ImageUsage::eSampled)
                              .setCubemap(true)
                              .build(m_RenderDevice)));

            m_RenderDevice.setupSampler(*prefilteredEnvMap,
                                        {
                                            .magFilter    = rhi::TexelFilter::eLinear,
                                            .minFilter    = rhi::TexelFilter::eLinear,
                                            .mipmapMode   = rhi::MipmapMode::eLinear,
                                            .addressModeS = rhi::SamplerAddressMode::eClampToEdge,
                                            .addressModeT = rhi::SamplerAddressMode::eClampToEdge,
                                            .addressModeR = rhi::SamplerAddressMode::eClampToEdge,
                                            .maxLod       = static_cast<float>(kMipLevels),
                                        });

            rhi::prepareForComputing(cb, *prefilteredEnvMap);

            constexpr auto kDescriptorSetId = 0;

            auto descriptors = cb.createDescriptorSetBuilder()
                                   .bind(0,
                                         rhi::bindings::CombinedImageSampler {.texture     = &cubemap,
                                                                              .imageAspect = rhi::ImageAspect::eColor})
                                   .bind(1,
                                         rhi::bindings::StorageImage {.texture     = prefilteredEnvMap.get(),
                                                                      .imageAspect = rhi::ImageAspect::eColor})
                                   .build(m_PrefilterEnvMapPipeline.getDescriptorSetLayout(kDescriptorSetId));

            for (uint32_t level = 0; level < kMipLevels; ++level)
            {
                uint32_t mipSize   = rhi::calcMipSize(glm::uvec3 {kSize, kSize, 1}, level).x;
                float    roughness = static_cast<float>(level) / static_cast<float>(kMipLevels - 1);

                struct PushConstants
                {
                    uint32_t mipLevel;
                    float    roughness;
                    uint32_t sampleCount;
                } pc {};

                pc.mipLevel    = level;
                pc.roughness   = roughness;
                pc.sampleCount = 1024u;

                cb.bindPipeline(m_PrefilterEnvMapPipeline)
                    .bindDescriptorSet(kDescriptorSetId, descriptors)
                    .pushConstants(rhi::ShaderStages::eCompute, 0, &pc)
                    .dispatch({(mipSize + 7) / 8, (mipSize + 7) / 8, 1});
            }

            rhi::prepareForReading(cb, *prefilteredEnvMap);
            return prefilteredEnvMap;
        }

        rhi::ComputePipeline IBLDataGenerator::createBrdfPipeline() const
        {
            return m_RenderDevice.createComputePipelineBuiltin(generate_brdf_comp_spv);
        }

        rhi::ComputePipeline IBLDataGenerator::createIrradiancePipeline() const
        {
            return m_RenderDevice.createComputePipelineBuiltin(generate_irradiance_map_comp_spv);
        }

        rhi::ComputePipeline IBLDataGenerator::createPrefilterEnvMapPipeline() const
        {
            return m_RenderDevice.createComputePipelineBuiltin(prefilter_envmap_comp_spv);
        }
    } // namespace gfx
} // namespace vultra