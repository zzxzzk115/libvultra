#include "vultra/core/rhi/interfaces/texture_access.hpp"

#include "vultra/core/rhi/texture.hpp"

namespace vultra
{
    namespace rhi
    {
        std::uintptr_t TextureAccess::getImageHandle(const Texture& texture) { return texture.getImageHandle(); }

        Texture TextureAccess::fromExternalImage(const RenderBackendApi api,
                                                 const std::uintptr_t   device,
                                                 const std::uintptr_t   image,
                                                 const Extent2D         extent,
                                                 const PixelFormat      format,
                                                 const uint32_t         baseLayer)
        {
            return Texture::fromExternalImage(api, device, image, extent, format, baseLayer);
        }

        Texture TextureAccess::fromExternalImage(const RenderBackendApi api,
                                                 const std::uintptr_t   device,
                                                 const std::uintptr_t   image,
                                                 const Extent2D         extent,
                                                 const PixelFormat      format,
                                                 const uint32_t         baseLayer,
                                                 const uint32_t         numLayers)
        {
            return Texture::fromExternalImage(api, device, image, extent, format, baseLayer, numLayers);
        }

        Texture TextureAccess::fromOwnedImage(const RenderBackendApi api,
                                              const std::uintptr_t   device,
                                              const std::uintptr_t   image,
                                              const Extent2D         extent,
                                              const PixelFormat      format,
                                              const uint32_t         baseLayer)
        {
            return Texture::fromOwnedImage(api, device, image, extent, format, baseLayer);
        }

        Texture TextureAccess::fromOwnedImage(const RenderBackendApi api,
                                              const std::uintptr_t   device,
                                              const std::uintptr_t   image,
                                              const Extent2D         extent,
                                              const PixelFormat      format,
                                              const uint32_t         baseLayer,
                                              const uint32_t         numLayers)
        {
            return Texture::fromOwnedImage(api, device, image, extent, format, baseLayer, numLayers);
        }
    } // namespace rhi
} // namespace vultra
