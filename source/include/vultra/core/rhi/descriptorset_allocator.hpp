#pragma once

#include "vultra/core/rhi/structs/handles.hpp"
#include "vultra/core/rhi/interfaces/idescriptor_set_allocator.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace vultra
{
    namespace rhi
    {
        class CommandBuffer;

        struct DescriptorPool
        {
            std::uintptr_t handle {0};
            uint32_t       numAllocatedSets {0};

            const static uint32_t s_kSetsPerPool;

            explicit DescriptorPool(std::uintptr_t h) : handle(h), numAllocatedSets(0) {}
        };

        class DescriptorSetAllocator final
        {
            friend class CommandBuffer;

        public:
            DescriptorSetAllocator()                              = default;
            DescriptorSetAllocator(const DescriptorSetAllocator&) = delete;
            DescriptorSetAllocator(DescriptorSetAllocator&&) noexcept;
            ~DescriptorSetAllocator();

            DescriptorSetAllocator& operator=(const DescriptorSetAllocator&) = delete;
            DescriptorSetAllocator& operator=(DescriptorSetAllocator&&) noexcept;

            // Internal constructor used by backend command buffers.
            explicit DescriptorSetAllocator(std::unique_ptr<IDescriptorSetAllocator> backend, bool raytracing = false);

            [[nodiscard]] DescriptorSetHandle allocate(std::uintptr_t descriptorSetLayout, uint32_t);
            void                            reset();

        private:
            void destroy() noexcept;

            [[nodiscard]] DescriptorPool&      createPool();
            [[nodiscard]] DescriptorPool&      getPool();
            [[nodiscard]] DescriptorSetHandle  allocate(DescriptorPool&, std::uintptr_t descriptorSetLayout, uint32_t) const;

        private:
            std::unique_ptr<IDescriptorSetAllocator> m_Backend;

            std::vector<DescriptorPool> m_DescriptorPools;
            int32_t                     m_LastPoolIndex {-1};
            bool                        m_EnableRaytracing {false};
        };
    } // namespace rhi
} // namespace vultra
