#include "vultra/core/rhi/draw_indirect_buffer.hpp"

namespace vultra::rhi
{
    namespace
    {
        [[nodiscard]] constexpr Buffer::Stride getIndexedStride() { return 5 * sizeof(uint32_t); }
        [[nodiscard]] constexpr Buffer::Stride getIndirectStride() { return 4 * sizeof(uint32_t); }
    } // namespace

    DrawIndirectType DrawIndirectBuffer::getDrawIndirectType() const { return m_Type; }

    Buffer::Stride DrawIndirectBuffer::getStride() const
    {
        return m_Type == DrawIndirectType::eIndexed ? getIndexedStride() : getIndirectStride();
    }

    uint64_t DrawIndirectBuffer::getCapacity() const { return getStride() > 0 ? getSize() / getStride() : 0; }

    DrawIndirectBuffer::DrawIndirectBuffer(Buffer&& buffer, const DrawIndirectType type) :
        Buffer(std::move(buffer)), m_Type(type)
    {}
} // namespace vultra::rhi
