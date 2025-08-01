#include "vultra/core/rhi/buffer.hpp"
#include "vultra/core/rhi/vk/macro.hpp"

namespace vultra
{
    namespace rhi
    {
        Buffer::Buffer(Buffer&& other) noexcept :
            m_MemoryAllocator(other.m_MemoryAllocator), m_Allocation(other.m_Allocation), m_Handle(other.m_Handle),
            m_LastScope(other.m_LastScope), m_Size(other.m_Size), m_MappedMemory(other.m_MappedMemory)
        {
            other.m_MemoryAllocator = nullptr;
            other.m_Allocation      = nullptr;
            other.m_Handle          = nullptr;
            other.m_LastScope       = {};
            other.m_Size            = 0;
            other.m_MappedMemory    = nullptr;
        }

        Buffer::~Buffer() { destroy(); }

        Buffer& Buffer::operator=(Buffer&& rhs) noexcept
        {
            if (this != &rhs)
            {
                destroy();

                std::swap(m_MemoryAllocator, rhs.m_MemoryAllocator);
                std::swap(m_Allocation, rhs.m_Allocation);
                std::swap(m_Handle, rhs.m_Handle);
                std::swap(m_LastScope, rhs.m_LastScope);
                std::swap(m_Size, rhs.m_Size);
                std::swap(m_MappedMemory, rhs.m_MappedMemory);
            }

            return *this;
        }

        Buffer::operator bool() const { return m_Handle != nullptr; }

        vk::Buffer Buffer::getHandle() const { return m_Handle; }

        vk::DeviceSize Buffer::getSize() const { return m_Size; }

        void* Buffer::map()
        {
            assert(m_Handle);

            if (!m_MappedMemory)
            {
                VK_CHECK(m_MemoryAllocator.mapMemory(m_Allocation, &m_MappedMemory), "Buffer", "Failed to map memory");
            }

            return m_MappedMemory;
        }

        Buffer& Buffer::unmap()
        {
            assert(m_Handle);

            if (m_MappedMemory)
            {
                m_MemoryAllocator.unmapMemory(m_Allocation);
                m_MappedMemory = nullptr;
            }

            return *this;
        }

        Buffer& Buffer::flush(const vk::DeviceSize offset, const vk::DeviceSize size)
        {
            assert(m_Handle && m_MappedMemory);

            m_MemoryAllocator.flushAllocation(m_Allocation, offset, size);
            return *this;
        }

        Buffer::Buffer(const vma::Allocator             memoryAllocator,
                       const vk::DeviceSize             size,
                       const vk::BufferUsageFlags       bufferUsage,
                       const vma::AllocationCreateFlags allocationFlags,
                       const vma::MemoryUsage           memoryUsage) : m_MemoryAllocator(memoryAllocator)
        {
            vk::BufferCreateInfo bufferCreateInfo {};
            bufferCreateInfo.size        = size;
            bufferCreateInfo.usage       = bufferUsage;
            bufferCreateInfo.sharingMode = vk::SharingMode::eExclusive;

            vma::AllocationCreateInfo memoryAllocationCreateInfo {};
            memoryAllocationCreateInfo.usage = memoryUsage;
            memoryAllocationCreateInfo.flags = allocationFlags;

            vma::AllocationInfo allocationInfo {};
            VK_CHECK(m_MemoryAllocator.createBuffer(
                         &bufferCreateInfo, &memoryAllocationCreateInfo, &m_Handle, &m_Allocation, &allocationInfo),
                     "Buffer",
                     "Failed to create buffer");

            m_Size = allocationInfo.size;
        }

        void Buffer::destroy() noexcept
        {
            if (m_Handle)
            {
                unmap();

                m_MemoryAllocator.destroyBuffer(m_Handle, m_Allocation);
                m_MemoryAllocator = nullptr;
                m_Allocation      = nullptr;
                m_Handle          = nullptr;
                m_Size            = 0;
                m_MappedMemory    = nullptr;
                m_LastScope       = {};
            }
        }
    } // namespace rhi
} // namespace vultra