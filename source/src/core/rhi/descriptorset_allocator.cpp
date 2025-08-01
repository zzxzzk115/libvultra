#include "vultra/core/rhi/descriptorset_allocator.hpp"

#include "vultra/core/rhi/vk/macro.hpp"

#include <array>

namespace vultra
{
    namespace rhi
    {

        const uint32_t DescriptorPool::s_kSetsPerPool = 100u;

        namespace
        {
            [[nodiscard]] auto createDescriptorPool(const vk::Device device)
            {
#define POOL_SIZE(Type, Multiplier) \
    vk::DescriptorPoolSize \
    { \
        vk::DescriptorType::Type, static_cast<uint32_t>(DescriptorPool::s_kSetsPerPool * Multiplier) \
    }
                // clang-format off
                constexpr auto kPoolSizes = std::array{
                    POOL_SIZE(eSampler, 0.26f),
                    POOL_SIZE(eCombinedImageSampler, 5.4f),
                    POOL_SIZE(eSampledImage, 1.81f),
                    POOL_SIZE(eStorageImage, 0.12f),
                    POOL_SIZE(eUniformBuffer, 2.2f),
                    POOL_SIZE(eStorageBuffer, 3.6f),
                  };
                // clang-format on
#undef POOL_SIZE

                vk::DescriptorPoolCreateInfo createInfo {};
                createInfo.maxSets       = DescriptorPool::s_kSetsPerPool;
                createInfo.poolSizeCount = static_cast<uint32_t>(kPoolSizes.size());
                createInfo.pPoolSizes    = kPoolSizes.data();

                vk::DescriptorPool descriptorPool {nullptr};
                VK_CHECK(device.createDescriptorPool(&createInfo, nullptr, &descriptorPool),
                         "DescriptorSetAllocator",
                         "Failed to create descriptor pool!");
                return descriptorPool;
            }

        } // namespace

        DescriptorSetAllocator::DescriptorSetAllocator(DescriptorSetAllocator&& other) noexcept :
            m_Device(other.m_Device), m_DescriptorPools(other.m_DescriptorPools), m_LastPoolIndex(other.m_LastPoolIndex)
        {
            other.m_Device = nullptr;
            other.m_DescriptorPools.clear();
            other.m_LastPoolIndex = -1;
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
            }

            return *this;
        }

        vk::DescriptorSet DescriptorSetAllocator::allocate(const vk::DescriptorSetLayout descriptorSetLayout)
        {
            assert(m_Device && descriptorSetLayout);
            auto descriptorSet = allocate(getPool(), descriptorSetLayout);
            if (descriptorSet == nullptr)
            {
                // No more space in the descriptor pool (any of .pPoolSizes)
                descriptorSet = allocate(createPool(), descriptorSetLayout);
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
                    m_Device.resetDescriptorPool(h);
                    numAllocatedSets = 0;
                }
            }
            m_LastPoolIndex = m_DescriptorPools.empty() ? -1 : 0;
        }

        DescriptorSetAllocator::DescriptorSetAllocator(const vk::Device device) : m_Device(device) { assert(device); }

        void DescriptorSetAllocator::destroy() noexcept
        {
            if (!m_Device)
            {
                assert(m_DescriptorPools.empty());
                return;
            }

            for (const auto [h, _] : m_DescriptorPools)
            {
                m_Device.destroyDescriptorPool(h);
            }
            m_DescriptorPools.clear();
            m_LastPoolIndex = -1;

            m_Device = nullptr;
        }

        DescriptorPool& DescriptorSetAllocator::createPool()
        {
            m_LastPoolIndex           = static_cast<int32_t>(m_DescriptorPools.size());
            const auto descriptorPool = createDescriptorPool(m_Device);
            return m_DescriptorPools.emplace_back(descriptorPool);
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

        vk::DescriptorSet DescriptorSetAllocator::allocate(DescriptorPool&               descriptorPool,
                                                           const vk::DescriptorSetLayout descriptorSetLayout) const
        {
            vk::DescriptorSetAllocateInfo allocateInfo {};
            allocateInfo.descriptorPool     = descriptorPool.handle;
            allocateInfo.descriptorSetCount = 1;
            allocateInfo.pSetLayouts        = &descriptorSetLayout;

            vk::DescriptorSet descriptorSet {};
            vk::Result        result = m_Device.allocateDescriptorSets(&allocateInfo, &descriptorSet);
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

            return descriptorSet;
        }
    } // namespace rhi
} // namespace vultra