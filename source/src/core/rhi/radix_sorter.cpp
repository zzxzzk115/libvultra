#include "vultra/core/rhi/radix_sorter.hpp"

#include "vultra/core/rhi/buffer.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/render_device.hpp"

#include <vk_radix_sort.h>

#include <cassert>
#include <utility>

namespace vultra
{
    namespace rhi
    {
        struct RadixSorter::Impl
        {
            uint32_t                        maxElementCount {0};
            VrdxSorter                      sorter {VK_NULL_HANDLE};
            RadixSorterStorageRequirements  storageRequirements {};
            RadixSorterStorageRequirements  keyValueStorageRequirements {};
        };

        RadixSorter::RadixSorter(std::unique_ptr<Impl>&& impl) : m_Impl(std::move(impl)) {}

        RadixSorter RadixSorter::create(RenderDevice& rd, const uint32_t maxElementCount)
        {
            assert(maxElementCount > 0u);

            auto impl                = std::make_unique<Impl>();
            impl->maxElementCount    = maxElementCount;

            VrdxSorterCreateInfo createInfo {};
            createInfo.physicalDevice = static_cast<VkPhysicalDevice>(rd.m_PhysicalDevice);
            createInfo.device         = static_cast<VkDevice>(rd.m_Device);
            createInfo.pipelineCache  = static_cast<VkPipelineCache>(rd.m_PipelineCache);

            vrdxCreateSorter(&createInfo, &impl->sorter);
            assert(impl->sorter != VK_NULL_HANDLE);

            VrdxSorterStorageRequirements storageRequirements {};
            vrdxGetSorterStorageRequirements(impl->sorter, maxElementCount, &storageRequirements);
            impl->storageRequirements.size  = storageRequirements.size;
            impl->storageRequirements.usage = vk::BufferUsageFlags(storageRequirements.usage);

            VrdxSorterStorageRequirements keyValueStorageRequirements {};
            vrdxGetSorterKeyValueStorageRequirements(impl->sorter, maxElementCount, &keyValueStorageRequirements);
            impl->keyValueStorageRequirements.size  = keyValueStorageRequirements.size;
            impl->keyValueStorageRequirements.usage = vk::BufferUsageFlags(keyValueStorageRequirements.usage);

            return RadixSorter {std::move(impl)};
        }

        RadixSorter::RadixSorter(RadixSorter&&) noexcept = default;

        RadixSorter::~RadixSorter()
        {
            if (m_Impl && m_Impl->sorter != VK_NULL_HANDLE)
            {
                vrdxDestroySorter(m_Impl->sorter);
                m_Impl->sorter = VK_NULL_HANDLE;
            }
        }

        RadixSorter& RadixSorter::operator=(RadixSorter&&) noexcept = default;

        RadixSorter::operator bool() const { return m_Impl && m_Impl->sorter != VK_NULL_HANDLE; }

        uint32_t RadixSorter::getMaxElementCount() const { return m_Impl ? m_Impl->maxElementCount : 0u; }

        RadixSorterStorageRequirements RadixSorter::getStorageRequirements() const
        {
            return m_Impl ? m_Impl->storageRequirements : RadixSorterStorageRequirements {};
        }

        RadixSorterStorageRequirements RadixSorter::getKeyValueStorageRequirements() const
        {
            return m_Impl ? m_Impl->keyValueStorageRequirements : RadixSorterStorageRequirements {};
        }

        void RadixSorter::sortKeys(CommandBuffer& cb,
                                   const uint32_t elementCount,
                                   const Buffer&  keys,
                                   const vk::DeviceSize keysOffset,
                                   const Buffer&  storage,
                                   const vk::DeviceSize storageOffset) const
        {
            assert(*this);
            if (elementCount <= 1u)
                return;

            vrdxCmdSort(static_cast<VkCommandBuffer>(cb.getHandle()),
                        m_Impl->sorter,
                        elementCount,
                        static_cast<VkBuffer>(keys.getHandle()),
                        keysOffset,
                        static_cast<VkBuffer>(storage.getHandle()),
                        storageOffset,
                        VK_NULL_HANDLE,
                        0u);
        }

        void RadixSorter::sortKeyValues(CommandBuffer& cb,
                                        const uint32_t elementCount,
                                        const Buffer&  keys,
                                        const vk::DeviceSize keysOffset,
                                        const Buffer&  values,
                                        const vk::DeviceSize valuesOffset,
                                        const Buffer&  storage,
                                        const vk::DeviceSize storageOffset) const
        {
            assert(*this);
            if (elementCount <= 1u)
                return;

            vrdxCmdSortKeyValue(static_cast<VkCommandBuffer>(cb.getHandle()),
                                m_Impl->sorter,
                                elementCount,
                                static_cast<VkBuffer>(keys.getHandle()),
                                keysOffset,
                                static_cast<VkBuffer>(values.getHandle()),
                                valuesOffset,
                                static_cast<VkBuffer>(storage.getHandle()),
                                storageOffset,
                                VK_NULL_HANDLE,
                                0u);
        }

        void RadixSorter::sortKeyValuesIndirect(CommandBuffer& cb,
                                                const uint32_t maxElementCount,
                                                const Buffer&  indirect,
                                                const vk::DeviceSize indirectOffset,
                                                const Buffer&  keys,
                                                const vk::DeviceSize keysOffset,
                                                const Buffer&  values,
                                                const vk::DeviceSize valuesOffset,
                                                const Buffer&  storage,
                                                const vk::DeviceSize storageOffset) const
        {
            assert(*this);
            if (maxElementCount <= 1u)
                return;

            vrdxCmdSortKeyValueIndirect(static_cast<VkCommandBuffer>(cb.getHandle()),
                                        m_Impl->sorter,
                                        maxElementCount,
                                        static_cast<VkBuffer>(indirect.getHandle()),
                                        indirectOffset,
                                        static_cast<VkBuffer>(keys.getHandle()),
                                        keysOffset,
                                        static_cast<VkBuffer>(values.getHandle()),
                                        valuesOffset,
                                        static_cast<VkBuffer>(storage.getHandle()),
                                        storageOffset,
                                        VK_NULL_HANDLE,
                                        0u);
        }
    } // namespace rhi
} // namespace vultra
