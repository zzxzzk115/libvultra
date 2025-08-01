#pragma once

#include "vultra/core/rhi/barrier_scope.hpp"
#include "vultra/core/rhi/image_layout.hpp"

#include <vector>

namespace vultra
{
    namespace rhi
    {
        class CommandBuffer;
        class Buffer;
        class Texture;

        class Barrier final
        {
            friend class CommandBuffer;

            struct Dependencies
            {
                Dependencies()
                {
                    buffer.reserve(10);
                    image.reserve(10);
                }

                std::vector<vk::MemoryBarrier2>       memory;
                std::vector<vk::BufferMemoryBarrier2> buffer;
                std::vector<vk::ImageMemoryBarrier2>  image;
            };

        public:
            [[nodiscard]] bool isEffective() const;

            class Builder
            {
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
                    const Buffer&  buffer;
                    vk::DeviceSize offset {0};
                    vk::DeviceSize size {vk::WholeSize};
                };
                Builder& bufferBarrier(const BufferInfo& info, const BarrierScope& dst);

                struct ImageInfo
                {
                    const Texture&            image;
                    ImageLayout               newLayout {ImageLayout::eUndefined};
                    vk::ImageSubresourceRange subresourceRange {vk::ImageAspectFlagBits::eNone,
                                                                0u,
                                                                vk::RemainingMipLevels,
                                                                0u,
                                                                vk::RemainingArrayLayers};
                };
                Builder& imageBarrier(ImageInfo info, const BarrierScope& dst);

                [[nodiscard]] Barrier build();

            private:
                Builder& imageBarrier(vk::Image,
                                      const BarrierScope&,
                                      const BarrierScope&,
                                      ImageLayout,
                                      ImageLayout,
                                      const vk::ImageSubresourceRange&);

            private:
                Dependencies m_Dependencies;
            };

        private:
            explicit Barrier(Dependencies&&);

        private:
            vk::DependencyInfo m_Info;
            Dependencies       m_Dependencies;
        };
    } // namespace rhi
} // namespace vultra