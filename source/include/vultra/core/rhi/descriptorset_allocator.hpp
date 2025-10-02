#pragma once

#include <vulkan/vulkan.hpp>

namespace vultra
{
    namespace rhi
    {
        class CommandBuffer;

        struct DescriptorPool
        {
            vk::DescriptorPool handle {nullptr};
            uint32_t           numAllocatedSets {0};

            const static uint32_t s_kSetsPerPool;

            explicit DescriptorPool(vk::DescriptorPool h) : handle(h), numAllocatedSets(0) {}
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

            [[nodiscard]] vk::DescriptorSet allocate(const vk::DescriptorSetLayout, uint32_t);
            void                            reset();

        private:
            explicit DescriptorSetAllocator(const vk::Device, bool raytracing = false);

            void destroy() noexcept;

            [[nodiscard]] DescriptorPool&   createPool();
            [[nodiscard]] DescriptorPool&   getPool();
            [[nodiscard]] vk::DescriptorSet allocate(DescriptorPool&, const vk::DescriptorSetLayout, uint32_t) const;

        private:
            vk::Device m_Device {nullptr};

            std::vector<DescriptorPool> m_DescriptorPools;
            int32_t                     m_LastPoolIndex {-1};
            bool                        m_EnableRaytracing {false};
        };
    } // namespace rhi
} // namespace vultra