#include "vultra/core/rhi/radix_sorter.hpp"

#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/interfaces/iradix_sorter.hpp"
#include "vultra/core/rhi/render_device.hpp"

#include <cassert>
#include <utility>

namespace vultra
{
    namespace rhi
    {
        RadixSorter::RadixSorter(std::unique_ptr<IRadixSorter>&& backend) : m_Backend(std::move(backend)) {}

        RadixSorter RadixSorter::create(std::unique_ptr<IRadixSorter>&& backend)
        {
            return RadixSorter {std::move(backend)};
        }

        RadixSorter RadixSorter::create(RenderDevice& rd, const uint32_t maxElementCount)
        {
            assert(maxElementCount > 0u);
            return rd.createRadixSorter(maxElementCount);
        }

        RadixSorter::RadixSorter(RadixSorter&&) noexcept            = default;
        RadixSorter::~RadixSorter()                                 = default;
        RadixSorter& RadixSorter::operator=(RadixSorter&&) noexcept = default;

        RadixSorter::operator bool() const { return m_Backend && static_cast<bool>(*m_Backend); }

        uint32_t RadixSorter::getMaxElementCount() const
        {
            assert(m_Backend);
            return m_Backend->getMaxElementCount();
        }

        RadixSorterStorageRequirements RadixSorter::getStorageRequirements() const
        {
            assert(m_Backend);
            return m_Backend->getStorageRequirements();
        }

        RadixSorterStorageRequirements RadixSorter::getKeyValueStorageRequirements() const
        {
            assert(m_Backend);
            return m_Backend->getKeyValueStorageRequirements();
        }

        void RadixSorter::sortKeys(CommandBuffer& cb,
                                   const uint32_t elementCount,
                                   const Buffer&  keys,
                                   const uint64_t keysOffset,
                                   const Buffer&  storage,
                                   const uint64_t storageOffset) const
        {
            assert(*this);
            m_Backend->sortKeys(cb, elementCount, keys, keysOffset, storage, storageOffset);
        }

        void RadixSorter::sortKeyValues(CommandBuffer& cb,
                                        const uint32_t elementCount,
                                        const Buffer&  keys,
                                        const uint64_t keysOffset,
                                        const Buffer&  values,
                                        const uint64_t valuesOffset,
                                        const Buffer&  storage,
                                        const uint64_t storageOffset) const
        {
            assert(*this);
            m_Backend->sortKeyValues(cb, elementCount, keys, keysOffset, values, valuesOffset, storage, storageOffset);
        }

        void RadixSorter::sortKeyValuesIndirect(CommandBuffer& cb,
                                                const uint32_t maxElementCount,
                                                const Buffer&  indirect,
                                                const uint64_t indirectOffset,
                                                const Buffer&  keys,
                                                const uint64_t keysOffset,
                                                const Buffer&  values,
                                                const uint64_t valuesOffset,
                                                const Buffer&  storage,
                                                const uint64_t storageOffset) const
        {
            assert(*this);
            m_Backend->sortKeyValuesIndirect(cb,
                                             maxElementCount,
                                             indirect,
                                             indirectOffset,
                                             keys,
                                             keysOffset,
                                             values,
                                             valuesOffset,
                                             storage,
                                             storageOffset);
        }
    } // namespace rhi
} // namespace vultra
