#include "vultra/core/rhi/interfaces/texture_access.hpp"

namespace vultra
{
    namespace rhi
    {
        std::uintptr_t TextureAccess::getImageHandle(const Texture& texture) { return texture.getImageHandle(); }

        Texture TextureAccess::fromExternalImage(const RenderBackendApi    api,
                                                 const TextureDeviceHandle device,
                                                 const TextureImageHandle  image,
                                                 const Extent2D            extent,
                                                 const PixelFormat         format,
                                                 const uint32_t            baseLayer)
        {
            return Texture::fromExternalImage(api, device, image, extent, format, baseLayer);
        }

        Texture TextureAccess::fromExternalImage(const RenderBackendApi    api,
                                                 const TextureDeviceHandle device,
                                                 const TextureImageHandle  image,
                                                 const Extent2D            extent,
                                                 const PixelFormat         format,
                                                 const uint32_t            baseLayer,
                                                 const uint32_t            numLayers)
        {
            return Texture::fromExternalImage(api, device, image, extent, format, baseLayer, numLayers);
        }

        Texture TextureAccess::fromOwnedImage(const RenderBackendApi    api,
                                              const TextureDeviceHandle device,
                                              const TextureImageHandle  image,
                                              const Extent2D            extent,
                                              const PixelFormat         format,
                                              const uint32_t            baseLayer,
                                              IRenderDevice*            renderDevice)
        {
            return Texture::fromOwnedImage(api, device, image, extent, format, baseLayer, renderDevice);
        }

        Texture TextureAccess::fromOwnedImage(const RenderBackendApi    api,
                                              const TextureDeviceHandle device,
                                              const TextureImageHandle  image,
                                              const Extent2D            extent,
                                              const PixelFormat         format,
                                              const uint32_t            baseLayer,
                                              const uint32_t            numLayers,
                                              IRenderDevice*            renderDevice)
        {
            return Texture::fromOwnedImage(api, device, image, extent, format, baseLayer, numLayers, renderDevice);
        }

        Texture TextureAccess::fromOwnedImage(const RenderBackendApi    api,
                                              const TextureDeviceHandle device,
                                              const TextureImageHandle  image,
                                              const Extent2D            extent,
                                              const PixelFormat         format,
                                              const uint32_t            baseLayer,
                                              const uint32_t            numLayers,
                                              const uint32_t            numMipLevels,
                                              IRenderDevice*            renderDevice)
        {
            return Texture::fromOwnedImage(
                api, device, image, extent, format, baseLayer, numLayers, numMipLevels, renderDevice);
        }
    } // namespace rhi
} // namespace vultra
