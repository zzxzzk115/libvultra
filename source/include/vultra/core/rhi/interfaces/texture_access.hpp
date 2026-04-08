#pragma once

#include "vultra/core/rhi/texture.hpp"

#include <cstdint>

namespace vultra
{
    namespace rhi
    {
        class IRenderDevice;

        class TextureAccess final
        {
        public:
            [[nodiscard]] static std::uintptr_t getImageHandle(const Texture&);

            [[nodiscard]] static Texture fromExternalImage(RenderBackendApi api,
                                                           TextureDeviceHandle device,
                                                           TextureImageHandle  image,
                                                           Extent2D            extent,
                                                           PixelFormat         format,
                                                           uint32_t            baseLayer = 0u);
            [[nodiscard]] static Texture fromExternalImage(RenderBackendApi api,
                                                           TextureDeviceHandle device,
                                                           TextureImageHandle  image,
                                                           Extent2D            extent,
                                                           PixelFormat         format,
                                                           uint32_t            baseLayer,
                                                           uint32_t            numLayers);

            [[nodiscard]] static Texture fromOwnedImage(RenderBackendApi api,
                                                        TextureDeviceHandle device,
                                                        TextureImageHandle  image,
                                                        Extent2D            extent,
                                                        PixelFormat         format,
                                                        uint32_t            baseLayer = 0u,
                                                        IRenderDevice*      renderDevice = nullptr);
            [[nodiscard]] static Texture fromOwnedImage(RenderBackendApi api,
                                                        TextureDeviceHandle device,
                                                        TextureImageHandle  image,
                                                        Extent2D            extent,
                                                        PixelFormat         format,
                                                        uint32_t            baseLayer,
                                                        uint32_t            numLayers,
                                                        IRenderDevice*      renderDevice = nullptr);
            [[nodiscard]] static Texture fromOwnedImage(RenderBackendApi api,
                                                        TextureDeviceHandle device,
                                                        TextureImageHandle  image,
                                                        Extent2D            extent,
                                                        PixelFormat         format,
                                                        uint32_t            baseLayer,
                                                        uint32_t            numLayers,
                                                        uint32_t            numMipLevels,
                                                        IRenderDevice*      renderDevice = nullptr);
        };
    } // namespace rhi
} // namespace vultra
