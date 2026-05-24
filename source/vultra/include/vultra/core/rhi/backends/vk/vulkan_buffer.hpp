#pragma once

#include "vultra/core/rhi/interfaces/ibuffer.hpp"

#include "vultra/core/rhi/structs/buffer_usage.hpp"
#include "vultra/core/rhi/structs/render_device_structs.hpp"

#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VULKAN_HPP_DISABLE_ENHANCED_MODE
#include <vk_mem_alloc.hpp>

namespace vultra
{
    namespace rhi
    {
        class IRenderDevice;

        class VulkanBuffer final : public IBuffer
        {
        public:
            VulkanBuffer(vma::Allocator,
                         uint64_t size,
                         BufferUsage,
                         vma::AllocationCreateFlags,
                         vma::MemoryUsage,
                         IRenderDevice* renderDevice = nullptr);
            ~VulkanBuffer() override;

            VulkanBuffer(const VulkanBuffer&)            = delete;
            VulkanBuffer(VulkanBuffer&&) noexcept        = delete;
            VulkanBuffer& operator=(const VulkanBuffer&)  = delete;
            VulkanBuffer& operator=(VulkanBuffer&&) noexcept = delete;

            [[nodiscard]] bool          isValid() const override;
            [[nodiscard]] std::uintptr_t getHandle() const override;
            [[nodiscard]] uint64_t      getSize() const override;
            [[nodiscard]] BarrierScope  getLastScope() const override;
            void                        setLastScope(BarrierScope) override;

            void* map() override;
            void  unmap() override;
            void  flush(uint64_t offset, uint64_t size) override;

        private:
            void destroy() noexcept;

        private:
            vma::Allocator  m_MemoryAllocator {nullptr};
            IRenderDevice*  m_RenderDevice {nullptr};
            RenderMemoryKind m_MemoryKind {RenderMemoryKind::eGpuDeviceLocal};
            vma::Allocation m_Allocation {nullptr};
            vk::Buffer      m_Handle {nullptr};
            uint64_t        m_Size {0};
            uint64_t        m_AllocationSize {0};
            void*           m_MappedMemory {nullptr};
            BarrierScope    m_LastScope {kInitialBarrierScope};
        };
    } // namespace rhi
} // namespace vultra
