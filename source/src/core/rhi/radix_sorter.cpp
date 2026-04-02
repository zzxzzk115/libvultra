#include "vultra/core/rhi/radix_sorter.hpp"

#include "vultra/core/rhi/interfaces/iradix_sorter_backend.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/render_device.hpp"

#include <cassert>
#include <utility>

namespace vultra
{
    namespace rhi
    {
        struct RadixSorter::Impl
        {
            std::unique_ptr<IRadixSorterBackend> backend;
        };

        RadixSorter::RadixSorter(std::unique_ptr<Impl>&& impl) : m_Impl(std::move(impl)) {}

        RadixSorter RadixSorter::create(std::unique_ptr<IRadixSorterBackend>&& backend)
        {
            auto impl = std::make_unique<Impl>();
            impl->backend = std::move(backend);
            return RadixSorter {std::move(impl)};
        }

        RadixSorter RadixSorter::create(RenderDevice& rd, const uint32_t maxElementCount)
        {
            assert(maxElementCount > 0u);
            return rd.createRadixSorter(maxElementCount);
        }

        RadixSorter::RadixSorter(RadixSorter&&) noexcept = default;
        RadixSorter::~RadixSorter()                      = default;
        RadixSorter& RadixSorter::operator=(RadixSorter&&) noexcept = default;

        RadixSorter::operator bool() const
        {
            return m_Impl && m_Impl->backend && static_cast<bool>(*m_Impl->backend);
        }

        uint32_t RadixSorter::getMaxElementCount() const
        {
            assert(m_Impl && m_Impl->backend);
            return m_Impl->backend->getMaxElementCount();
        }

        RadixSorterStorageRequirements RadixSorter::getStorageRequirements() const
        {
            assert(m_Impl && m_Impl->backend);
            return m_Impl->backend->getStorageRequirements();
        }

        RadixSorterStorageRequirements RadixSorter::getKeyValueStorageRequirements() const
        {
            assert(m_Impl && m_Impl->backend);
            return m_Impl->backend->getKeyValueStorageRequirements();
        }

        void RadixSorter::sortKeys(CommandBuffer& cb,
                                   const uint32_t elementCount,
                                   const Buffer&  keys,
                                   const uint64_t keysOffset,
                                   const Buffer&  storage,
                                   const uint64_t storageOffset) const
        {
            assert(*this);
            m_Impl->backend->sortKeys(cb, elementCount, keys, keysOffset, storage, storageOffset);
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
            m_Impl->backend->sortKeyValues(cb, elementCount, keys, keysOffset, values, valuesOffset, storage, storageOffset);
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
            m_Impl->backend->sortKeyValuesIndirect(
                cb, maxElementCount, indirect, indirectOffset, keys, keysOffset, values, valuesOffset, storage, storageOffset);
        }
    } // namespace rhi
} // namespace vultra
