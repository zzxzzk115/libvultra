#include "vultra/core/rhi/descriptorset_allocator.hpp"

#include <cassert>

namespace vultra
{
    namespace rhi
    {

        const uint32_t DescriptorPool::s_kSetsPerPool = 100u;

        DescriptorSetAllocator::DescriptorSetAllocator(DescriptorSetAllocator&& other) noexcept :
            m_Backend(std::move(other.m_Backend)), m_DescriptorPools(other.m_DescriptorPools),
            m_LastPoolIndex(other.m_LastPoolIndex), m_EnableRaytracing(other.m_EnableRaytracing)
        {
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

                std::swap(m_Backend, rhs.m_Backend);
                std::swap(m_DescriptorPools, rhs.m_DescriptorPools);
                std::swap(m_LastPoolIndex, rhs.m_LastPoolIndex);
                std::swap(m_EnableRaytracing, rhs.m_EnableRaytracing);
            }

            return *this;
        }

        DescriptorSetHandle DescriptorSetAllocator::allocate(const std::uintptr_t descriptorSetLayout,
                                                             const uint32_t       variableDescriptorCount)
        {
            assert(m_Backend && descriptorSetLayout != 0);
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
            assert(m_Backend);
            for (auto& [h, numAllocatedSets] : m_DescriptorPools)
            {
                if (numAllocatedSets > 0)
                {
                    m_Backend->resetPool(h);
                    numAllocatedSets = 0;
                }
            }
            m_LastPoolIndex = m_DescriptorPools.empty() ? -1 : 0;
        }

        DescriptorSetAllocator::DescriptorSetAllocator(std::unique_ptr<IDescriptorSetAllocatorBackend> backend,
                                                       const bool                                       raytracing) :
            m_Backend(std::move(backend)), m_EnableRaytracing(raytracing)
        {
            assert(m_Backend);
        }

        void DescriptorSetAllocator::destroy() noexcept
        {
            if (!m_Backend)
            {
                assert(m_DescriptorPools.empty());
                return;
            }

            for (const auto [h, _] : m_DescriptorPools)
            {
                m_Backend->destroyPool(h);
            }
            m_DescriptorPools.clear();
            m_LastPoolIndex    = -1;
            m_EnableRaytracing = false;
            m_Backend.reset();
        }

        DescriptorPool& DescriptorSetAllocator::createPool()
        {
            m_LastPoolIndex           = static_cast<int32_t>(m_DescriptorPools.size());
            const auto descriptorPool = m_Backend->createPool(DescriptorPool::s_kSetsPerPool, m_EnableRaytracing);
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

        DescriptorSetHandle DescriptorSetAllocator::allocate(DescriptorPool&          descriptorPool,
                                                             const std::uintptr_t      descriptorSetLayout,
                                                             const uint32_t            variableDescriptorCount) const
        {
            const auto descriptorSet = m_Backend->allocateDescriptorSet(
                descriptorPool.handle, descriptorSetLayout, variableDescriptorCount);
            if (descriptorSet.value != 0)
            {
                descriptorPool.numAllocatedSets++;
            }
            return descriptorSet;
        }
    } // namespace rhi
} // namespace vultra
