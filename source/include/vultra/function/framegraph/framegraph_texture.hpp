#pragma once

#include "vultra/core/rhi/extent2d.hpp"
#include "vultra/core/rhi/image_usage.hpp"
#include "vultra/core/rhi/pixel_format.hpp"

namespace vultra
{
    namespace rhi
    {
        class Texture;
    }

    namespace framegraph
    {
        class FrameGraphTexture
        {
        public:
            struct Desc
            {
                rhi::Extent2D    extent;
                uint32_t         depth {0};
                rhi::PixelFormat format {rhi::PixelFormat::eUndefined};
                uint32_t         numMipLevels {1};
                uint32_t         layers {0};
                bool             cubemap {false};
                rhi::ImageUsage  usageFlags;
            };

            void create(const Desc&, void* allocator);
            void destroy(const Desc&, void* allocator);

            void preRead(const Desc&, uint32_t bits, void* ctx) const;
            void preWrite(const Desc&, uint32_t bits, void* ctx) const;

            [[nodiscard]] static std::string toString(const Desc&);

            rhi::Texture* texture {nullptr};
        };
    } // namespace framegraph
} // namespace vultra