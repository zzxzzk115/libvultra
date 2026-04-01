#pragma once

#include "vultra/core/rhi/buffer.hpp"
#include "vultra/core/rhi/structs/radix_sorter_types.hpp"

namespace vultra
{
    namespace rhi
    {
        class CommandBuffer;

        class IRadixSorterBackend
        {
        public:
            virtual ~IRadixSorterBackend() = default;

            [[nodiscard]] virtual explicit operator bool() const = 0;
            [[nodiscard]] virtual uint32_t                       getMaxElementCount() const                = 0;
            [[nodiscard]] virtual RadixSorterStorageRequirements getStorageRequirements() const           = 0;
            [[nodiscard]] virtual RadixSorterStorageRequirements getKeyValueStorageRequirements() const = 0;

            virtual void sortKeys(CommandBuffer&,
                                  uint32_t       elementCount,
                                  const Buffer&  keys,
                                  uint64_t       keysOffset,
                                  const Buffer&  storage,
                                  uint64_t       storageOffset) const = 0;

            virtual void sortKeyValues(CommandBuffer&,
                                       uint32_t       elementCount,
                                       const Buffer&  keys,
                                       uint64_t       keysOffset,
                                       const Buffer&  values,
                                       uint64_t       valuesOffset,
                                       const Buffer&  storage,
                                       uint64_t       storageOffset) const = 0;

            virtual void sortKeyValuesIndirect(CommandBuffer&,
                                               uint32_t       maxElementCount,
                                               const Buffer&  indirect,
                                               uint64_t       indirectOffset,
                                               const Buffer&  keys,
                                               uint64_t       keysOffset,
                                               const Buffer&  values,
                                               uint64_t       valuesOffset,
                                               const Buffer&  storage,
                                               uint64_t       storageOffset) const = 0;
        };
    } // namespace rhi
} // namespace vultra
