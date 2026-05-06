#pragma once

#include "vultra/core/rhi/interfaces/iradix_sorter.hpp"
#include "vultra/core/rhi/structs/radix_sorter_types.hpp"
#include <vk_radix_sort.h>

namespace vultra
{
    namespace rhi
    {
        class RenderDevice;

        class VulkanRadixSorter final : public IRadixSorter
        {
        public:
            VulkanRadixSorter(const RenderDevice&, uint32_t maxElementCount);
            ~VulkanRadixSorter() override;

            [[nodiscard]] explicit operator bool() const override;
            [[nodiscard]] uint32_t                       getMaxElementCount() const override;
            [[nodiscard]] RadixSorterStorageRequirements getStorageRequirements() const override;
            [[nodiscard]] RadixSorterStorageRequirements getKeyValueStorageRequirements() const override;

            void sortKeys(CommandBuffer&,
                          uint32_t       elementCount,
                          const Buffer&  keys,
                          uint64_t       keysOffset,
                          const Buffer&  storage,
                          uint64_t       storageOffset) const override;

            void sortKeyValues(CommandBuffer&,
                               uint32_t       elementCount,
                               const Buffer&  keys,
                               uint64_t       keysOffset,
                               const Buffer&  values,
                               uint64_t       valuesOffset,
                               const Buffer&  storage,
                               uint64_t       storageOffset) const override;

            void sortKeyValuesIndirect(CommandBuffer&,
                                       uint32_t       maxElementCount,
                                       const Buffer&  indirect,
                                       uint64_t       indirectOffset,
                                       const Buffer&  keys,
                                       uint64_t       keysOffset,
                                       const Buffer&  values,
                                       uint64_t       valuesOffset,
                                       const Buffer&  storage,
                                       uint64_t       storageOffset) const override;

        private:
            uint32_t                       m_MaxElementCount {0};
            VrdxSorter                     m_Sorter {nullptr};
            RadixSorterStorageRequirements m_StorageRequirements {};
            RadixSorterStorageRequirements m_KeyValueStorageRequirements {};
        };
    } // namespace rhi
} // namespace vultra
