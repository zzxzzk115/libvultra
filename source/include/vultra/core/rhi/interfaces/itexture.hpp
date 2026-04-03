#pragma once

#include "vultra/core/rhi/structs/barrier_scope.hpp"
#include "vultra/core/rhi/structs/cube_face.hpp"
#include "vultra/core/rhi/structs/extent2d.hpp"
#include "vultra/core/rhi/structs/image_aspect.hpp"
#include "vultra/core/rhi/structs/image_layout.hpp"
#include "vultra/core/rhi/structs/image_usage.hpp"
#include "vultra/core/rhi/structs/pixel_format.hpp"
#include "vultra/core/rhi/structs/texture_type.hpp"
#include "vultra/core/rhi/texture_view.hpp"

#include <cstdint>
#include <optional>
#include <span>

namespace vultra
{
    namespace rhi
    {
        class ITexture
        {
        public:
            virtual ~ITexture() = default;

            [[nodiscard]] virtual bool isValid() const = 0;

            [[nodiscard]] virtual TextureType getType() const = 0;
            [[nodiscard]] virtual Extent2D    getExtent() const = 0;
            [[nodiscard]] virtual uint32_t    getDepth() const = 0;
            [[nodiscard]] virtual uint32_t    getNumMipLevels() const = 0;
            [[nodiscard]] virtual uint32_t    getNumLayers() const = 0;
            [[nodiscard]] virtual PixelFormat getPixelFormat() const = 0;
            [[nodiscard]] virtual ImageUsage  getUsageFlags() const = 0;

            [[nodiscard]] virtual std::uintptr_t getImageHandle() const = 0;
            [[nodiscard]] virtual ImageLayout    getImageLayout() const = 0;
            [[nodiscard]] virtual uint32_t       getBaseArrayLayer() const = 0;
            [[nodiscard]] virtual uint32_t       getLayerFaceCount() const = 0;
            [[nodiscard]] virtual BarrierScope   getLastBarrierScope() const = 0;
            virtual void                         setBarrierState(BarrierScope, ImageLayout) = 0;

            [[nodiscard]] virtual uint64_t getSize() const = 0;

            [[nodiscard]] virtual TextureView getImageView(ImageAspectFlags) const = 0;
            [[nodiscard]] virtual TextureView getMipLevel(uint32_t, ImageAspectFlags) const = 0;
            [[nodiscard]] virtual std::span<const TextureView> getMipLevels(ImageAspectFlags) const = 0;
            [[nodiscard]] virtual TextureView getLayer(uint32_t, std::optional<CubeFace>, ImageAspectFlags) const = 0;
            [[nodiscard]] virtual std::span<const TextureView> getLayers(ImageAspectFlags) const = 0;
        };
    } // namespace rhi
} // namespace vultra
