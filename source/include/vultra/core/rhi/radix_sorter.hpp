#pragma once

#include <cstdint>
#include <memory>

#include "vultra/core/rhi/interfaces/iradix_sorter_backend.hpp"
#include "vultra/core/rhi/structs/radix_sorter_types.hpp"
 
namespace vultra
{
    namespace rhi
    {
        class Buffer;
        class CommandBuffer;
        class RenderDevice;

        class RadixSorter final
        {
            friend class RenderDevice;

        public:
            RadixSorter()                   = default;
            RadixSorter(const RadixSorter&) = delete;
            RadixSorter(RadixSorter&&) noexcept;
            ~RadixSorter();

            RadixSorter& operator=(const RadixSorter&) = delete;
            RadixSorter& operator=(RadixSorter&&) noexcept;

            [[nodiscard]] explicit operator bool() const;

            [[nodiscard]] uint32_t                       getMaxElementCount() const;
            [[nodiscard]] RadixSorterStorageRequirements getStorageRequirements() const;
            [[nodiscard]] RadixSorterStorageRequirements getKeyValueStorageRequirements() const;

            void sortKeys(CommandBuffer&,
                          uint32_t       elementCount,
                          const Buffer&  keys,
                          uint64_t       keysOffset,
                          const Buffer&  storage,
                          uint64_t       storageOffset) const;

            void sortKeyValues(CommandBuffer&,
                               uint32_t       elementCount,
                               const Buffer&  keys,
                               uint64_t       keysOffset,
                               const Buffer&  values,
                               uint64_t       valuesOffset,
                               const Buffer&  storage,
                               uint64_t       storageOffset) const;

            void sortKeyValuesIndirect(CommandBuffer&,
                                       uint32_t       maxElementCount,
                                       const Buffer&  indirect,
                                       uint64_t       indirectOffset,
                                       const Buffer&  keys,
                                       uint64_t       keysOffset,
                                       const Buffer&  values,
                                       uint64_t       valuesOffset,
                                       const Buffer&  storage,
                                       uint64_t       storageOffset) const;

        private:
            explicit RadixSorter(std::unique_ptr<IRadixSorterBackend>&&);
            static RadixSorter create(std::unique_ptr<IRadixSorterBackend>&&);
            static RadixSorter create(RenderDevice&, uint32_t maxElementCount);

        private:
            std::unique_ptr<IRadixSorterBackend> m_Backend;
        };
    } // namespace rhi
} // namespace vultra
