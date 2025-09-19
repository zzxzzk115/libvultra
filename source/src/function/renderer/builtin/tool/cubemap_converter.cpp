#include "vultra/function/renderer/builtin/tool/cubemap_converter.hpp"
#include "vultra/core/rhi/command_buffer.hpp"

#include <shader_headers/cubemap_convert.comp.spv.h>

namespace vultra
{
    namespace gfx
    {
        CubemapConverter::CubemapConverter(rhi::RenderDevice& rd) : m_RenderDevice(rd)
        {
            m_ConvertPipeline = m_RenderDevice.createComputePipelineBuiltin(cubemap_convert_comp_spv);
        }

        Ref<rhi::Texture> CubemapConverter::convertToCubemap(rhi::CommandBuffer& cb, rhi::Texture& equirectangularMap)
        {
            constexpr auto kSize            = 1024u;
            constexpr auto kDescriptorSetId = 0;

            auto cubemap = createRef<rhi::Texture>(
                std::move(rhi::Texture::Builder {}
                              .setExtent({kSize, kSize})
                              .setPixelFormat(rhi::PixelFormat::eRGBA16F)
                              .setNumMipLevels(1)
                              .setUsageFlags(rhi::ImageUsage::eStorage | rhi::ImageUsage::eSampled)
                              .setCubemap(true)
                              .build(m_RenderDevice)));

            m_RenderDevice.setupSampler(*cubemap,
                                        {
                                            .magFilter    = rhi::TexelFilter::eLinear,
                                            .minFilter    = rhi::TexelFilter::eLinear,
                                            .mipmapMode   = rhi::MipmapMode::eNearest,
                                            .addressModeS = rhi::SamplerAddressMode::eClampToEdge,
                                            .addressModeT = rhi::SamplerAddressMode::eClampToEdge,
                                            .addressModeR = rhi::SamplerAddressMode::eClampToEdge,
                                        });

            rhi::prepareForComputing(cb, *cubemap);

            auto descriptors =
                cb.createDescriptorSetBuilder()
                    .bind(0,
                          rhi::bindings::CombinedImageSampler {.texture     = &equirectangularMap,
                                                               .imageAspect = rhi::ImageAspect::eColor})
                    .bind(1,
                          rhi::bindings::StorageImage {
                              .texture = cubemap.get(), .imageAspect = rhi::ImageAspect::eColor, .mipLevel = 0})
                    .build(m_ConvertPipeline.getDescriptorSetLayout(kDescriptorSetId));

            cb.bindPipeline(m_ConvertPipeline)
                .bindDescriptorSet(kDescriptorSetId, descriptors)
                .dispatch({(kSize + 7) / 8, (kSize + 7) / 8, 1});

            rhi::prepareForReading(cb, *cubemap);
            return cubemap;
        }
    } // namespace gfx
} // namespace vultra