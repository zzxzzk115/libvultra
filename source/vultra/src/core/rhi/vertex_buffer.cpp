#include "vultra/core/rhi/vertex_buffer.hpp"

namespace vultra
{
    namespace rhi
    {
        Buffer::Stride VertexBuffer::getStride() const { return m_Stride; }

        uint64_t VertexBuffer::getCapacity() const { return m_Stride > 0 ? getSize() / m_Stride : 0; }

        VertexBuffer::VertexBuffer(Buffer&& buffer, const Stride stride) : Buffer(std::move(buffer)), m_Stride(stride)
        {}
    } // namespace rhi
} // namespace vultra
