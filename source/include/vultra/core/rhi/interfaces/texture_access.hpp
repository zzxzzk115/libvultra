#pragma once

#include "vultra/core/rhi/structs/extent2d.hpp"
#include "vultra/core/rhi/structs/pixel_format.hpp"
#include "vultra/core/rhi/structs/render_backend_api.hpp"

#include <cstdint>

namespace vultra
{
    namespace rhi
    {
        class Texture;

        class TextureAccess final
        {
        public:
            [[nodiscard]] static std::uintptr_t getImageHandle(const Texture&);

            [[nodiscard]] static Texture
            fromExternalImage(RenderBackendApi api,
                              std::uintptr_t   device,
                              std::uintptr_t   image,
                              Extent2D         extent,
                              PixelFormat      format,
                              uint32_t         baseLayer = 0u);
            [[nodiscard]] static Texture fromExternalImage(RenderBackendApi api,
                                                           std::uintptr_t   device,
                                                           std::uintptr_t   image,
                                                           Extent2D         extent,
                                                           PixelFormat      format,
                                                           uint32_t         baseLayer,
                                                           uint32_t         numLayers);

            [[nodiscard]] static Texture
            fromOwnedImage(RenderBackendApi api,
                           std::uintptr_t   device,
                           std::uintptr_t   image,
                           Extent2D         extent,
                           PixelFormat      format,
                           uint32_t         baseLayer = 0u);
            [[nodiscard]] static Texture fromOwnedImage(RenderBackendApi api,
                                                        std::uintptr_t   device,
                                                        std::uintptr_t   image,
                                                        Extent2D         extent,
                                                        PixelFormat      format,
                                                        uint32_t         baseLayer,
                                                        uint32_t         numLayers);
        };
    } // namespace rhi
} // namespace vultra
