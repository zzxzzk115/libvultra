#include "vultra/core/rhi/backends/vk/vulkan_descriptor_set_allocator.hpp"

#include "vultra/core/rhi/backends/vk/handle_utils.hpp"
#include "vultra/core/rhi/backends/vk/macro.hpp"

#include <vector>

namespace vultra
{
    namespace rhi
    {
        namespace
        {
            [[nodiscard]] vk::DescriptorPool createDescriptorPool(const vk::Device device,
                                                                  const bool       raytracing,
                                                                  const uint32_t   setsPerPool)
            {
#define POOL_SIZE(Type, Multiplier) \
    vk::DescriptorPoolSize \
    { \
        vk::DescriptorType::Type, static_cast<uint32_t>(setsPerPool * Multiplier) \
    }
                auto poolSizes = std::vector<vk::DescriptorPoolSize> {
                    POOL_SIZE(eSampler, 0.26f),
                    POOL_SIZE(eCombinedImageSampler, 10.24f),
                    POOL_SIZE(eSampledImage, 1.81f),
                    POOL_SIZE(eStorageImage, 0.12f),
                    POOL_SIZE(eUniformBuffer, 2.2f),
                    POOL_SIZE(eStorageBuffer, 3.6f),
                };

                if (raytracing)
                {
                    poolSizes.emplace_back(POOL_SIZE(eStorageBufferDynamic, 1.0f));
                    poolSizes.emplace_back(POOL_SIZE(eAccelerationStructureKHR, 1.0f));
                }
#undef POOL_SIZE

                vk::DescriptorPoolCreateInfo createInfo {};
                createInfo.maxSets       = setsPerPool;
                createInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
                createInfo.pPoolSizes    = poolSizes.data();
#if __APPLE__
                createInfo.flags = vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind;
#endif

                vk::DescriptorPool descriptorPool {nullptr};
                VK_CHECK(device.createDescriptorPool(&createInfo, nullptr, &descriptorPool),
                         "DescriptorSetAllocator",
                         "Failed to create descriptor pool!");
                return descriptorPool;
            }
        } // namespace

        VulkanDescriptorSetAllocator::VulkanDescriptorSetAllocator(const std::uintptr_t deviceHandle) :
            m_Device(vk::Device {asVkHandle<VkDevice>(deviceHandle)})
        {}

        std::uintptr_t VulkanDescriptorSetAllocator::createPool(const uint32_t setsPerPool, const bool raytracing)
        {
            const auto pool = createDescriptorPool(m_Device, raytracing, setsPerPool);
            return toBackendHandle(static_cast<VkDescriptorPool>(pool));
        }

        void VulkanDescriptorSetAllocator::resetPool(const std::uintptr_t poolHandle)
        {
            m_Device.resetDescriptorPool(vk::DescriptorPool {asVkHandle<VkDescriptorPool>(poolHandle)});
        }

        void VulkanDescriptorSetAllocator::destroyPool(const std::uintptr_t poolHandle)
        {
            m_Device.destroyDescriptorPool(vk::DescriptorPool {asVkHandle<VkDescriptorPool>(poolHandle)});
        }

        DescriptorSetHandle VulkanDescriptorSetAllocator::allocateDescriptorSet(
            const std::uintptr_t poolHandle,
            const std::uintptr_t descriptorSetLayoutHandle,
            const uint32_t       variableDescriptorCount)
        {
            vk::DescriptorSetVariableDescriptorCountAllocateInfo countInfo {};
            countInfo.descriptorSetCount = 1;
            countInfo.pDescriptorCounts  = &variableDescriptorCount;

            vk::DescriptorSetAllocateInfo allocateInfo {};
            allocateInfo.descriptorPool     = vk::DescriptorPool {asVkHandle<VkDescriptorPool>(poolHandle)};
            allocateInfo.descriptorSetCount = 1;
            const auto layout               = vk::DescriptorSetLayout {asVkHandle<VkDescriptorSetLayout>(descriptorSetLayoutHandle)};
            allocateInfo.pSetLayouts        = &layout;
            allocateInfo.pNext              = variableDescriptorCount > 0 ? &countInfo : nullptr;

            vk::DescriptorSet descriptorSet {};
            const auto        result = m_Device.allocateDescriptorSets(&allocateInfo, &descriptorSet);
            switch (result)
            {
                case vk::Result::eSuccess:
                case vk::Result::eErrorOutOfPoolMemory:
                case vk::Result::eErrorFragmentedPool:
                    break;
                default:
                    assert(false);
            }

            return DescriptorSetHandle {toBackendHandle(static_cast<VkDescriptorSet>(descriptorSet))};
        }
    } // namespace rhi
} // namespace vultra
