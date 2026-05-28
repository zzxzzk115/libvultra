#include "vultra/core/rhi/backends/vk/vulkan_buffer.hpp"

#include "vultra/core/rhi/backends/vk/handle_utils.hpp"
#include "vultra/core/rhi/backends/vk/macro.hpp"
#include "vultra/core/rhi/interfaces/irender_device.hpp"

#include <format>

namespace vultra::rhi
{
    namespace
    {
        [[nodiscard]] constexpr vk::BufferUsageFlags toVk(const BufferUsage usage)
        {
            vk::BufferUsageFlags out {};
            if (HasFlagValues(usage, BufferUsage::eTransferSrc))
                out |= vk::BufferUsageFlagBits::eTransferSrc;
            if (HasFlagValues(usage, BufferUsage::eTransferDst))
                out |= vk::BufferUsageFlagBits::eTransferDst;
            if (HasFlagValues(usage, BufferUsage::eVertexBuffer))
                out |= vk::BufferUsageFlagBits::eVertexBuffer;
            if (HasFlagValues(usage, BufferUsage::eIndexBuffer))
                out |= vk::BufferUsageFlagBits::eIndexBuffer;
            if (HasFlagValues(usage, BufferUsage::eUniformBuffer))
                out |= vk::BufferUsageFlagBits::eUniformBuffer;
            if (HasFlagValues(usage, BufferUsage::eStorageBuffer))
                out |= vk::BufferUsageFlagBits::eStorageBuffer;
            if (HasFlagValues(usage, BufferUsage::eIndirectBuffer))
                out |= vk::BufferUsageFlagBits::eIndirectBuffer;
            if (HasFlagValues(usage, BufferUsage::eShaderDeviceAddress))
                out |= vk::BufferUsageFlagBits::eShaderDeviceAddress;
            if (HasFlagValues(usage, BufferUsage::eAccelerationBuildInput))
                out |= vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR;
            if (HasFlagValues(usage, BufferUsage::eAccelerationStorage))
                out |= vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR;
            if (HasFlagValues(usage, BufferUsage::eShaderBindingTable))
                out |= vk::BufferUsageFlagBits::eShaderBindingTableKHR;
            return out;
        }

        [[nodiscard]] const char* bufferMemoryKindLabel(const RenderMemoryKind kind)
        {
            switch (kind)
            {
                case RenderMemoryKind::eCpuCache:
                    return "CPU";
                case RenderMemoryKind::eGpuDeviceLocal:
                    return "GPU";
                case RenderMemoryKind::eGpuHostVisible:
                    return "Host";
            }
            return "Memory";
        }
    } // namespace

    VulkanBuffer::VulkanBuffer(const vma::Allocator             memoryAllocator,
                               const uint64_t                   size,
                               const BufferUsage                bufferUsage,
                               const vma::AllocationCreateFlags allocationFlags,
                               const vma::MemoryUsage           memoryUsage,
                               IRenderDevice*                   renderDevice) :
        m_MemoryAllocator(memoryAllocator), m_RenderDevice(renderDevice)
    {
        m_MemoryKind = memoryUsage == vma::MemoryUsage::eGpuOnly ? RenderMemoryKind::eGpuDeviceLocal :
                                                                   RenderMemoryKind::eGpuHostVisible;

        vk::BufferCreateInfo bufferCreateInfo {};
        bufferCreateInfo.size        = size;
        bufferCreateInfo.usage       = toVk(bufferUsage);
        bufferCreateInfo.sharingMode = vk::SharingMode::eExclusive;

        vma::AllocationCreateInfo memoryAllocationCreateInfo {};
        memoryAllocationCreateInfo.usage = memoryUsage;
        memoryAllocationCreateInfo.flags = allocationFlags;

        vma::AllocationInfo allocationInfo {};
        VK_CHECK(m_MemoryAllocator.createBuffer(
                     &bufferCreateInfo, &memoryAllocationCreateInfo, &m_Handle, &m_Allocation, &allocationInfo),
                 "VulkanBuffer",
                 "Failed to create buffer");

        m_Size           = size;
        m_AllocationSize = allocationInfo.size;
        if (m_RenderDevice)
        {
            m_RenderDevice->onMemoryAllocated(m_MemoryKind, m_AllocationSize);
            m_RenderDevice->onMemoryResourceAllocated(RenderMemoryResourceDesc {
                .id      = static_cast<uint64_t>(getHandle()),
                .type    = RenderMemoryResourceType::eBuffer,
                .kind    = m_MemoryKind,
                .bytes   = m_AllocationSize,
                .label   = std::format("{} Buffer 0x{:x}", bufferMemoryKindLabel(m_MemoryKind), getHandle()),
                .details = std::format("Buffer requested={} allocation={} usage=0x{:x}",
                                       m_Size,
                                       m_AllocationSize,
                                       static_cast<uint32_t>(bufferUsage)),
            });
        }
    }

    VulkanBuffer::~VulkanBuffer() { destroy(); }

    bool VulkanBuffer::isValid() const { return m_Handle != nullptr; }

    std::uintptr_t VulkanBuffer::getHandle() const { return toBackendHandle(static_cast<VkBuffer>(m_Handle)); }

    uint64_t VulkanBuffer::getSize() const { return m_Size; }

    BarrierScope VulkanBuffer::getLastScope() const { return m_LastScope; }

    void VulkanBuffer::setLastScope(const BarrierScope scope) { m_LastScope = scope; }

    void* VulkanBuffer::map()
    {
        assert(m_Handle);

        if (!m_MappedMemory)
        {
            VK_CHECK(
                m_MemoryAllocator.mapMemory(m_Allocation, &m_MappedMemory), "VulkanBuffer", "Failed to map memory");
        }

        return m_MappedMemory;
    }

    void VulkanBuffer::unmap()
    {
        assert(m_Handle);

        if (m_MappedMemory)
        {
            m_MemoryAllocator.unmapMemory(m_Allocation);
            m_MappedMemory = nullptr;
        }
    }

    void VulkanBuffer::flush(const uint64_t offset, const uint64_t size)
    {
        assert(m_Handle && m_MappedMemory);
        VK_CHECK(m_MemoryAllocator.flushAllocation(m_Allocation, offset, size),
                 "[VulkanBuffer]",
                 "Failed to flush allocation");
    }

    void VulkanBuffer::destroy() noexcept
    {
        if (m_Handle)
        {
            unmap();

            m_MemoryAllocator.destroyBuffer(m_Handle, m_Allocation);
            if (m_RenderDevice)
            {
                m_RenderDevice->onMemoryResourceFreed(static_cast<uint64_t>(getHandle()));
                m_RenderDevice->onMemoryFreed(m_MemoryKind, m_AllocationSize);
            }
            m_MemoryAllocator = nullptr;
            m_RenderDevice    = nullptr;
            m_Allocation      = nullptr;
            m_Handle          = nullptr;
            m_Size            = 0;
            m_AllocationSize  = 0;
            m_MappedMemory    = nullptr;
            m_LastScope       = {};
        }
    }
} // namespace vultra::rhi
