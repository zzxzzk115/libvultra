#pragma once

#include "vultra/core/rhi/structs/native_handles.hpp"

#include <cstdint>
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
            explicit DescriptorSetAllocator(std::uintptr_t deviceHandle, bool raytracing = false);

            [[nodiscard]] DescriptorSetHandle allocate(std::uintptr_t descriptorSetLayout, uint32_t);
            void                            reset();

        private:
            void destroy() noexcept;

            [[nodiscard]] DescriptorPool&      createPool();
            [[nodiscard]] DescriptorPool&      getPool();
            [[nodiscard]] DescriptorSetHandle  allocate(DescriptorPool&, std::uintptr_t descriptorSetLayout, uint32_t) const;

        private:
            std::uintptr_t m_Device {0};

            std::vector<DescriptorPool> m_DescriptorPools;
            int32_t                     m_LastPoolIndex {-1};
            bool                        m_EnableRaytracing {false};
        };
    } // namespace rhi
} // namespace vultra
