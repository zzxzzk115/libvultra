#pragma once

#include "vultra/core/rhi/barrier_scope.hpp"

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

    } // namespace rhi
} // namespace vultra