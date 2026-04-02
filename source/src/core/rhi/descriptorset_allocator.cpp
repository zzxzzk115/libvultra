#include "vultra/core/rhi/descriptorset_allocator.hpp"

#include "vultra/core/rhi/backends/vk/macro.hpp"

#include <vulkan/vulkan.hpp>

namespace vultra
{
    namespace rhi
    {

        const uint32_t DescriptorPool::s_kSetsPerPool = 100u;

        namespace
        {
            [[nodiscard]] auto createDescriptorPool(const vk::Device device, bool raytracing = false)
            {
#define POOL_SIZE(Type, Multiplier) \
    vk::DescriptorPoolSize \
    { \
        vk::DescriptorType::Type, static_cast<uint32_t>(DescriptorPool::s_kSetsPerPool * Multiplier) \
    }
                // clang-format off
                auto poolSizes = std::vector<vk::DescriptorPoolSize>{
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
                // clang-format on
#undef POOL_SIZE

                vk::DescriptorPoolCreateInfo createInfo {};
                createInfo.maxSets       = DescriptorPool::s_kSetsPerPool;
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

        DescriptorSetAllocator::DescriptorSetAllocator(DescriptorSetAllocator&& other) noexcept :
            m_Device(other.m_Device), m_DescriptorPools(other.m_DescriptorPools),
            m_LastPoolIndex(other.m_LastPoolIndex), m_EnableRaytracing(other.m_EnableRaytracing)
        {
            other.m_Device = 0;
            other.m_DescriptorPools.clear();
            other.m_LastPoolIndex    = -1;
            other.m_EnableRaytracing = false;
        }

        DescriptorSetAllocator::~DescriptorSetAllocator() { destroy(); }

        DescriptorSetAllocator& DescriptorSetAllocator::operator=(DescriptorSetAllocator&& rhs) noexcept
        {
            if (this != &rhs)
            {
                destroy();

                std::swap(m_Device, rhs.m_Device);
                std::swap(m_DescriptorPools, rhs.m_DescriptorPools);
                std::swap(m_LastPoolIndex, rhs.m_LastPoolIndex);
                std::swap(m_EnableRaytracing, rhs.m_EnableRaytracing);
            }

            return *this;
        }

        DescriptorSetHandle DescriptorSetAllocator::allocate(const std::uintptr_t descriptorSetLayout,
                                                             const uint32_t       variableDescriptorCount)
        {
            assert(m_Device && descriptorSetLayout != 0);
            auto descriptorSet = allocate(getPool(), descriptorSetLayout, variableDescriptorCount);
            if (!descriptorSet)
            {
                // No more space in the descriptor pool (any of .pPoolSizes)
                descriptorSet = allocate(createPool(), descriptorSetLayout, variableDescriptorCount);
            }
            assert(descriptorSet);
            return descriptorSet;
        }

        void DescriptorSetAllocator::reset()
        {
            assert(m_Device);
            for (auto& [h, numAllocatedSets] : m_DescriptorPools)
            {
                if (numAllocatedSets > 0)
                {
                    const auto device = vk::Device {reinterpret_cast<VkDevice>(m_Device)};
                    device.resetDescriptorPool(vk::DescriptorPool {reinterpret_cast<VkDescriptorPool>(h)});
                    numAllocatedSets = 0;
                }
            }
            m_LastPoolIndex = m_DescriptorPools.empty() ? -1 : 0;
        }

        DescriptorSetAllocator::DescriptorSetAllocator(const std::uintptr_t deviceHandle, const bool raytracing) :
            m_Device(deviceHandle), m_EnableRaytracing(raytracing)
        {
            assert(deviceHandle != 0);
        }

        void DescriptorSetAllocator::destroy() noexcept
        {
            if (m_Device == 0)
            {
                assert(m_DescriptorPools.empty());
                return;
            }

            const auto device = vk::Device {reinterpret_cast<VkDevice>(m_Device)};
            for (const auto [h, _] : m_DescriptorPools)
            {
                device.destroyDescriptorPool(vk::DescriptorPool {reinterpret_cast<VkDescriptorPool>(h)});
            }
            m_DescriptorPools.clear();
            m_LastPoolIndex    = -1;
            m_EnableRaytracing = false;

            m_Device = 0;
        }

        DescriptorPool& DescriptorSetAllocator::createPool()
        {
            m_LastPoolIndex           = static_cast<int32_t>(m_DescriptorPools.size());
            const auto device = vk::Device {reinterpret_cast<VkDevice>(m_Device)};
            const auto descriptorPool = createDescriptorPool(device, m_EnableRaytracing);
            return m_DescriptorPools.emplace_back(reinterpret_cast<std::uintptr_t>(static_cast<VkDescriptorPool>(descriptorPool)));
        }

        DescriptorPool& DescriptorSetAllocator::getPool()
        {
            // NOTE: Compiler will convert m_lastPoolIndex to size_t (-1 < 0u == false)
            for (; m_LastPoolIndex < m_DescriptorPools.size(); ++m_LastPoolIndex)
            {
                if (auto& dp = m_DescriptorPools[m_LastPoolIndex]; dp.numAllocatedSets < DescriptorPool::s_kSetsPerPool)
                    return dp;
            }
            return createPool();
        }

        DescriptorSetHandle DescriptorSetAllocator::allocate(DescriptorPool&          descriptorPool,
                                                             const std::uintptr_t      descriptorSetLayout,
                                                             const uint32_t            variableDescriptorCount) const
        {
            const auto device = vk::Device {reinterpret_cast<VkDevice>(m_Device)};
            vk::DescriptorSetVariableDescriptorCountAllocateInfo countInfo {};
            countInfo.descriptorSetCount = 1;
            countInfo.pDescriptorCounts  = &variableDescriptorCount;

            vk::DescriptorSetAllocateInfo allocateInfo {};
            allocateInfo.descriptorPool     = vk::DescriptorPool {reinterpret_cast<VkDescriptorPool>(descriptorPool.handle)};
            allocateInfo.descriptorSetCount = 1;
            const auto layout               = vk::DescriptorSetLayout {reinterpret_cast<VkDescriptorSetLayout>(descriptorSetLayout)};
            allocateInfo.pSetLayouts        = &layout;
            allocateInfo.pNext              = variableDescriptorCount > 0 ? &countInfo : nullptr;

            vk::DescriptorSet descriptorSet {};
            vk::Result        result = device.allocateDescriptorSets(&allocateInfo, &descriptorSet);
            switch (result)
            {
                case vk::Result::eSuccess:
                case vk::Result::eErrorOutOfPoolMemory:
                case vk::Result::eErrorFragmentedPool:
                    break;

                default:
                    assert(false);
            }
            if (descriptorSet != nullptr)
            {
                descriptorPool.numAllocatedSets++;
            }

            return DescriptorSetHandle {reinterpret_cast<std::uintptr_t>(static_cast<VkDescriptorSet>(descriptorSet))};
        }
    } // namespace rhi
} // namespace vultra
