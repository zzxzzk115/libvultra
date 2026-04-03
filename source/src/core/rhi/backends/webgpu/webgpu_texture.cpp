#include "vultra/core/rhi/texture.hpp"

#include "vultra/core/rhi/backends/webgpu/conversions.hpp"

#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
#include <webgpu/webgpu.h>
#endif

namespace vultra::rhi
{
    namespace
    {
        [[nodiscard]] bool isLayeredTextureType(const TextureType textureType)
        {
            switch (textureType)
            {
                case TextureType::eTexture2DArray:
                case TextureType::eTextureCube:
                case TextureType::eTextureCubeArray:
                    return true;
                default:
                    return false;
            }
        }

#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
        [[nodiscard]] TextureView createWgpuImageView(WGPUTexture                    texture,
                                                      const WGPUTextureViewDimension dimension,
                                                      const WGPUTextureFormat        format,
                                                      const WGPUTextureAspect        aspect,
                                                      const uint32_t                 baseMipLevel,
                                                      const uint32_t                 mipLevelCount,
                                                      const uint32_t                 baseArrayLayer,
                                                      const uint32_t                 arrayLayerCount)
        {
            WGPUTextureViewDescriptor viewDesc {};
            viewDesc.format          = format;
            viewDesc.dimension       = dimension;
            viewDesc.aspect          = aspect;
            viewDesc.baseMipLevel    = baseMipLevel;
            viewDesc.mipLevelCount   = mipLevelCount;
            viewDesc.baseArrayLayer  = baseArrayLayer;
            viewDesc.arrayLayerCount = arrayLayerCount;
            auto* const view         = wgpuTextureCreateView(texture, &viewDesc);
            return TextureView {reinterpret_cast<std::uintptr_t>(view)};
        }
#endif
    } // namespace

    void Texture::initImportedNativeAspects(const TextureImageHandle imageHandle, const PixelFormat pixelFormat)
    {
#if !defined(VULTRA_ENABLE_WEBGPU) || !VULTRA_ENABLE_WEBGPU
        (void)imageHandle;
        (void)pixelFormat;
#else
        const auto dimension = webgpu::toWgpuTextureViewDimension(m_Type);
        createAspectNative(imageHandle,
                           static_cast<uint32_t>(dimension),
                           ImageAspectFlags::eColor,
                           m_Aspects[static_cast<uint32_t>(ImageAspectFlags::eColor)]);
        if (HasFlagValues(getAspectMask(pixelFormat), ImageAspectFlags::eDepth) &&
            HasFlagValues(getAspectMask(pixelFormat), ImageAspectFlags::eStencil))
        {
            createAspectNative(imageHandle,
                               static_cast<uint32_t>(dimension),
                               ImageAspectFlags::eDepth,
                               m_Aspects[static_cast<uint32_t>(ImageAspectFlags::eDepth)]);
            createAspectNative(imageHandle,
                               static_cast<uint32_t>(dimension),
                               ImageAspectFlags::eStencil,
                               m_Aspects[static_cast<uint32_t>(ImageAspectFlags::eStencil)]);
        }
        else if (!HasFlagValues(getAspectMask(pixelFormat), ImageAspectFlags::eColor))
        {
            const auto aspectMask = getAspectMask(pixelFormat);
            createAspectNative(imageHandle,
                               static_cast<uint32_t>(dimension),
                               aspectMask,
                               m_Aspects[static_cast<uint32_t>(aspectMask)]);
        }
#endif
    }

    void Texture::createAspectNative(const TextureImageHandle imageHandle,
                                     const uint32_t           viewType,
                                     const ImageAspectFlags   aspectMask,
                                     AspectData&              data)
    {
#if !defined(VULTRA_ENABLE_WEBGPU) || !VULTRA_ENABLE_WEBGPU
        (void)imageHandle;
        (void)viewType;
        (void)aspectMask;
        (void)data;
#else
        auto* const texture      = reinterpret_cast<WGPUTexture>(imageHandle.value);
        const auto  format       = webgpu::toWgpuTextureFormat(m_Format);
        const auto  wgpuViewType = static_cast<WGPUTextureViewDimension>(viewType);
        const auto  wgpuAspect   = webgpu::toWgpuTextureAspect(aspectMask);

        data.imageView =
            createWgpuImageView(texture, wgpuViewType, format, wgpuAspect, 0u, m_NumMipLevels, 0u, m_LayerFaces);

        data.mipLevels.reserve(m_NumMipLevels);
        for (auto i = 0u; i < m_NumMipLevels; ++i)
        {
            data.mipLevels.emplace_back(
                createWgpuImageView(texture, wgpuViewType, format, wgpuAspect, i, 1u, 0u, m_LayerFaces));
        }

        if (isLayeredTextureType(m_Type))
        {
            data.layers.reserve(m_LayerFaces);
            for (auto i = 0u; i < m_LayerFaces; ++i)
            {
                data.layers.emplace_back(
                    createWgpuImageView(texture, WGPUTextureViewDimension_2D, format, wgpuAspect, 0u, 1u, i, 1u));
            }
        }
#endif
    }

    void Texture::destroyNativeResources() noexcept
    {
#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
        for (auto& [_, data] : m_Aspects)
        {
            for (const auto layer : data.layers)
            {
                if (layer)
                {
                    wgpuTextureViewRelease(reinterpret_cast<WGPUTextureView>(layer.getHandle()));
                }
            }
            data.layers.clear();
            for (const auto mipLevel : data.mipLevels)
            {
                if (mipLevel)
                {
                    wgpuTextureViewRelease(reinterpret_cast<WGPUTextureView>(mipLevel.getHandle()));
                }
            }
            data.mipLevels.clear();

            if (data.imageView)
            {
                wgpuTextureViewRelease(reinterpret_cast<WGPUTextureView>(data.imageView.getHandle()));
                data.imageView = {};
            }
        }

        if (m_OwnsImage)
        {
            if (const auto imageHandle = getImageHandle(); imageHandle != 0)
            {
                wgpuTextureRelease(reinterpret_cast<WGPUTexture>(imageHandle));
            }
        }
#endif
    }
} // namespace vultra::rhi
