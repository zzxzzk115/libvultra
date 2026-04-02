#pragma once

#include "vultra/core/rhi/structs/barrier_scope.hpp"
#include "vultra/core/rhi/buffer.hpp"
#include "vultra/core/rhi/structs/image_aspect.hpp"
#include "vultra/core/rhi/structs/image_layout.hpp"
#include "vultra/core/rhi/structs/texture_type.hpp"

#include <cstdint>
#include <vector>

namespace vultra
{
    namespace rhi
    {
        class CommandBuffer;
        class Texture;

        struct ImageSubresourceRange
        {
            ImageAspectFlags aspectMask {ImageAspectFlags::eNone};
            uint32_t         baseMipLevel {0u};
            uint32_t         levelCount {UINT32_MAX};
            uint32_t         baseArrayLayer {0u};
            uint32_t         layerCount {UINT32_MAX};
        };

        struct BarrierMemory
        {
            BarrierScope src;
            BarrierScope dst;
        };

        struct BarrierBuffer
        {
            const Buffer* buffer {nullptr};
            uint64_t      offset {0};
            uint64_t      size {UINT64_MAX};
            BarrierScope   src;
            BarrierScope   dst;
        };

        struct BarrierImage
        {
            const Texture*        image {nullptr};
            ImageLayout           oldLayout {ImageLayout::eUndefined};
            ImageLayout           newLayout {ImageLayout::eUndefined};
            ImageSubresourceRange subresourceRange {};
            BarrierScope          src;
            BarrierScope          dst;
        };

        class Barrier final
        {
            friend class CommandBuffer;

        public:
            [[nodiscard]] bool isEffective() const;
            [[nodiscard]] const std::vector<BarrierMemory>& getMemoryBarriers() const;
            [[nodiscard]] const std::vector<BarrierBuffer>& getBufferBarriers() const;
            [[nodiscard]] const std::vector<BarrierImage>&  getImageBarriers() const;

            class Builder
            {
                friend class Barrier;
                friend class CommandBuffer;

            public:
                Builder()                   = default;
                Builder(const Builder&)     = delete;
                Builder(Builder&&) noexcept = default;
                ~Builder()                  = default;

                Builder& operator=(const Builder&)     = delete;
                Builder& operator=(Builder&&) noexcept = default;

                Builder& memoryBarrier(const BarrierScope& src, const BarrierScope& dst);

                struct BufferInfo
                {
                    Buffer&       buffer;
                    uint64_t      offset {0};
                    uint64_t      size {UINT64_MAX};
                };
                Builder& bufferBarrier(const BufferInfo& info, const BarrierScope& dst);

                struct ImageInfo
                {
                    Texture&              image;
                    ImageLayout           newLayout {ImageLayout::eUndefined};
                    ImageSubresourceRange subresourceRange {};
                };
                Builder& imageBarrier(ImageInfo info, const BarrierScope& dst);

                [[nodiscard]] Barrier build();

            private:
                std::vector<BarrierMemory> m_MemoryBarriers;
                std::vector<BarrierBuffer> m_BufferBarriers;
                std::vector<BarrierImage>  m_ImageBarriers;
            };

        private:
            explicit Barrier(Builder&&);

        private:
            std::vector<BarrierMemory> m_MemoryBarriers;
            std::vector<BarrierBuffer> m_BufferBarriers;
            std::vector<BarrierImage>  m_ImageBarriers;
        };

    } // namespace rhi
} // namespace vultra
