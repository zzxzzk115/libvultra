#pragma once

#include "vultra/core/rhi/interfaces/idescriptor_set_allocator.hpp"

#include <vulkan/vulkan.hpp>

namespace vultra
{
    namespace rhi
    {
        class VulkanDescriptorSetAllocator final : public IDescriptorSetAllocator
        {
        public:
            explicit VulkanDescriptorSetAllocator(std::uintptr_t deviceHandle);

            [[nodiscard]] std::uintptr_t createPool(uint32_t setsPerPool, bool raytracing) override;
            void                         resetPool(std::uintptr_t poolHandle) override;
            void                         destroyPool(std::uintptr_t poolHandle) override;
            [[nodiscard]] DescriptorSetHandle
            allocateDescriptorSet(std::uintptr_t poolHandle, std::uintptr_t descriptorSetLayoutHandle, uint32_t variableDescriptorCount) override;

        private:
            vk::Device m_Device {nullptr};
        };
    } // namespace rhi
} // namespace vultra

