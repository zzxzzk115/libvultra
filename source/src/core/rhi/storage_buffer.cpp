#include "vultra/core/rhi/storage_buffer.hpp"

namespace vultra
{
    namespace rhi
    {
        uint64_t StorageBuffer::getCapacity() const { return getSize(); }

        StorageBuffer::StorageBuffer(Buffer&& buffer) : Buffer(std::move(buffer)) {}
    } // namespace rhi
} // namespace vultra
