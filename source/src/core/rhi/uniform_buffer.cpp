#include "vultra/core/rhi/uniform_buffer.hpp"

namespace vultra
{
    namespace rhi
    {
        uint64_t UniformBuffer::getCapacity() const { return getSize(); }

        UniformBuffer::UniformBuffer(Buffer&& buffer) : Buffer(std::move(buffer)) {}
    } // namespace rhi
} // namespace vultra
