#pragma once

#include "vultra/core/rhi/barrier_scope.hpp"

#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VULKAN_HPP_DISABLE_ENHANCED_MODE
#include <vk_mem_alloc.hpp>

namespace vultra
{
    namespace rhi
    {
        class RenderDevice;
        class Barrier;

        class Buffer
        {
            friend class RenderDevice;
            friend class Barrier;

        public:
            Buffer()              = default;
            Buffer(const Buffer&) = delete;
            Buffer(Buffer&&) noexcept;
            virtual ~Buffer();

            Buffer& operator=(const Buffer&) = delete;
            Buffer& operator=(Buffer&&) noexcept;

            [[nodiscard]] explicit operator bool() const;

            using Stride = uint32_t;

            [[nodiscard]] vk::Buffer     getHandle() const;
            [[nodiscard]] vk::DeviceSize getSize() const;

            void*   map();
            Buffer& unmap();

            Buffer& flush(vk::DeviceSize offset = 0, vk::DeviceSize size = vk::WholeSize);

        private:
            Buffer(vma::Allocator,
                   vk::DeviceSize size,
                   vk::BufferUsageFlags,
                   vma::AllocationCreateFlags,
                   vma::MemoryUsage);

            void destroy() noexcept;

        private:
            vma::Allocator       m_MemoryAllocator {nullptr};
            vma::Allocation      m_Allocation {nullptr};
            vk::Buffer           m_Handle {nullptr};
            mutable BarrierScope m_LastScope {kInitialBarrierScope};

            vk::DeviceSize m_Size {0};
            void*          m_MappedMemory {nullptr};
        };

        using BufferCopy = vk::BufferCopy;

    } // namespace rhi
} // namespace vultra
