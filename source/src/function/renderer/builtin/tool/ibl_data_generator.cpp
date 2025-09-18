#include "vultra/function/renderer/builtin/tool/ibl_data_generator.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/function/renderer/builtin/post_process_helper.hpp"
#include "vultra/function/renderer/builtin/tool/cubemap_capture.hpp"

#include <shader_headers/generate_brdf.frag.spv.h>

#include <glm/glm.hpp>

namespace vultra
{
    namespace gfx
    {
        IBLDataGenerator::IBLDataGenerator(rhi::RenderDevice& rd) : m_RenderDevice(rd)
        {
            // Create pipelines
            m_BrdfPipeline            = createBrdfPipeline();
            m_IrradiancePipeline      = createIrradiancePipeline();
            m_PrefilterEnvMapPipeline = createPrefilterEnvMapPipeline();
        }

        rhi::Texture IBLDataGenerator::generateBrdfLUT(rhi::CommandBuffer& cb)
        {
            RHI_GPU_ZONE(cb, "Generate BRDF LUT");
            constexpr auto kSize = 1024u;

            auto brdf = rhi::Texture::Builder {}
                            .setExtent({kSize, kSize})
                            .setPixelFormat(rhi::PixelFormat::eRG16F)
                            .setNumMipLevels(1)
                            .setUsageFlags(rhi::ImageUsage::eStorage | rhi::ImageUsage::eSampled)
                            .build(m_RenderDevice);

            m_RenderDevice.setupSampler(brdf,
                                        {
                                            .magFilter  = rhi::TexelFilter::eLinear,
                                            .minFilter  = rhi::TexelFilter::eLinear,
                                            .mipmapMode = rhi::MipmapMode::eNearest,

                                            .addressModeS = rhi::SamplerAddressMode::eClampToEdge,
                                            .addressModeT = rhi::SamplerAddressMode::eClampToEdge,
                                        });

            rhi::prepareForAttachment(cb, brdf, false);

            constexpr auto kDescriptorSetId = 0;

            const auto descriptors = cb.createDescriptorSetBuilder()
                                         .bind(0,
                                               rhi::bindings::CombinedImageSampler {
                                                   .texture     = &brdf,
                                                   .imageAspect = rhi::ImageAspect::eColor,
                                               })
                                         .build(m_BrdfPipeline.getDescriptorSetLayout(kDescriptorSetId));

            cb.beginRendering({.area             = {0, 0, kSize, kSize},
                               .colorAttachments = {{
                                   .target     = &brdf,
                                   .clearValue = glm::vec4(0.0f),
                               }}});

            cb.bindPipeline(m_BrdfPipeline).bindDescriptorSet(kDescriptorSetId, descriptors).drawFullScreenTriangle();
            cb.endRendering();

            rhi::prepareForReading(cb, brdf);

            return brdf;
        }

        rhi::Texture IBLDataGenerator::generateIrradiance(rhi::CommandBuffer& cb, rhi::Texture& cubemap)
        {
            RHI_GPU_ZONE(cb, "Generate Irradiance Map");

            constexpr auto kSize = 64u;

            auto irradiance = rhi::Texture::Builder {}
                                  .setExtent({kSize, kSize})
                                  .setPixelFormat(rhi::PixelFormat::eRGB16F)
                                  .setNumMipLevels(1)
                                  .setNumLayers(6)
                                  .setUsageFlags(rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled)
                                  .setCubemap(true)
                                  .build(m_RenderDevice);

            m_RenderDevice.setupSampler(irradiance,
                                        {
                                            .magFilter  = rhi::TexelFilter::eLinear,
                                            .minFilter  = rhi::TexelFilter::eLinear,
                                            .mipmapMode = rhi::MipmapMode::eNearest,

                                            .addressModeS = rhi::SamplerAddressMode::eClampToEdge,
                                            .addressModeT = rhi::SamplerAddressMode::eClampToEdge,
                                            .addressModeR = rhi::SamplerAddressMode::eClampToEdge,
                                        });

            rhi::prepareForAttachment(cb, irradiance, true);

            struct PushConstants
            {
                glm::mat4 viewProjection;
            };

            constexpr auto kDescriptorSetId = 0;

            for (uint8_t face = 0; face < 6; ++face)
            {
                // Descriptor set for sampling input cubemap
                const auto descriptors = cb.createDescriptorSetBuilder()
                                             .bind(0,
                                                   rhi::bindings::CombinedImageSampler {
                                                       .texture     = &cubemap,
                                                       .imageAspect = rhi::ImageAspect::eColor,
                                                   })
                                             .build(m_IrradiancePipeline.getDescriptorSetLayout(kDescriptorSetId));

                cb.beginRendering({.area             = {0, 0, kSize, kSize},
                                   .colorAttachments = {{
                                       .target     = &irradiance,
                                       .layer      = face,
                                       .clearValue = glm::vec4(0.0f),
                                   }}});

                PushConstants pc {};
                pc.viewProjection = kCubeProjection * kCaptureViews[face];

                cb.bindPipeline(m_IrradiancePipeline)
                    .bindDescriptorSet(kDescriptorSetId, descriptors)
                    .pushConstants(rhi::ShaderStages::eVertex, 0, &pc)
                    .drawCube();

                cb.endRendering();
            }

            rhi::prepareForReading(cb, irradiance);

            return irradiance;
        }

        rhi::Texture IBLDataGenerator::generatePrefilterEnvMap(rhi::CommandBuffer& cb, rhi::Texture& cubemap)
        {
            RHI_GPU_ZONE(cb, "Prefilter Environment Map");

            constexpr auto kSize = 512u;

            auto prefilteredEnvMap = rhi::Texture::Builder {}
                                         .setExtent({kSize, kSize})
                                         .setPixelFormat(rhi::PixelFormat::eRGB16F)
                                         .setNumMipLevels(0) // let backend calc full mip chain
                                         .setNumLayers(6)
                                         .setUsageFlags(rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled)
                                         .setCubemap(true)
                                         .build(m_RenderDevice);

            m_RenderDevice.setupSampler(prefilteredEnvMap,
                                        {
                                            .magFilter  = rhi::TexelFilter::eLinear,
                                            .minFilter  = rhi::TexelFilter::eLinear,
                                            .mipmapMode = rhi::MipmapMode::eLinear,

                                            .addressModeS = rhi::SamplerAddressMode::eClampToEdge,
                                            .addressModeT = rhi::SamplerAddressMode::eClampToEdge,
                                            .addressModeR = rhi::SamplerAddressMode::eClampToEdge,
                                        });

            rhi::prepareForAttachment(cb, prefilteredEnvMap, true);

            struct PushConstantsVertex
            {
                glm::mat4 viewProjection;
            };

            struct PushConstantsFragment
            {
                float roughness;
            };

            const auto     numMipLevels     = prefilteredEnvMap.getNumMipLevels();
            constexpr auto kDescriptorSetId = 0;

            for (uint32_t level = 0; level < numMipLevels; ++level)
            {
                const auto  mipSize   = rhi::calcMipSize(glm::uvec3 {kSize, kSize, 1}, level).x;
                const float roughness = static_cast<float>(level) / std::max(1u, numMipLevels - 1u);

                for (uint32_t face = 0; face < 6; ++face)
                {
                    const auto descriptors =
                        cb.createDescriptorSetBuilder()
                            .bind(0,
                                  rhi::bindings::CombinedImageSampler {
                                      .texture     = &cubemap,
                                      .imageAspect = rhi::ImageAspect::eColor,
                                  })
                            .build(m_PrefilterEnvMapPipeline.getDescriptorSetLayout(kDescriptorSetId));

                    cb.beginRendering({.area             = {0, 0, mipSize, mipSize},
                                       .colorAttachments = {{
                                           .target     = &prefilteredEnvMap,
                                           .layer      = level,
                                           .face       = static_cast<rhi::CubeFace>(face),
                                           .clearValue = glm::vec4(0.0f),
                                       }}});

                    PushConstantsVertex vertexPushConstants {};
                    vertexPushConstants.viewProjection = kCubeProjection * kCaptureViews[face];

                    PushConstantsFragment fragmentPushConstants {};
                    fragmentPushConstants.roughness = roughness;

                    cb.bindPipeline(m_PrefilterEnvMapPipeline)
                        .bindDescriptorSet(kDescriptorSetId, descriptors)
                        .pushConstants(rhi::ShaderStages::eVertex, 0, &vertexPushConstants)
                        .pushConstants(
                            rhi::ShaderStages::eFragment, sizeof(PushConstantsVertex), &fragmentPushConstants)
                        .drawCube();

                    cb.endRendering();
                }
            }

            rhi::prepareForReading(cb, prefilteredEnvMap);

            return prefilteredEnvMap;
        }

        rhi::GraphicsPipeline IBLDataGenerator::createBrdfPipeline() const
        {
            return createPostProcessPipelineFromSPV(m_RenderDevice, rhi::PixelFormat::eRG16F, generate_brdf_frag_spv);
        }

        rhi::GraphicsPipeline IBLDataGenerator::createIrradiancePipeline() const { return {}; }

        rhi::GraphicsPipeline IBLDataGenerator::createPrefilterEnvMapPipeline() const { return {}; }
    } // namespace gfx
} // namespace vultra