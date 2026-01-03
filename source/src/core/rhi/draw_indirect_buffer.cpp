#include "vultra/core/rhi/draw_indirect_buffer.hpp"
#include "vultra/core/rhi/draw_indirect_type.hpp"

namespace vultra
{
    namespace rhi
    {
        DrawIndirectType DrawIndirectBuffer::getDrawIndirectType() const { return m_Type; }

        Buffer::Stride DrawIndirectBuffer::getStride() const { return m_Stride; }

        vk::DeviceSize DrawIndirectBuffer::getCapacity() const { return m_Stride > 0 ? getSize() / m_Stride : 0; }

        DrawIndirectBuffer::DrawIndirectBuffer(Buffer&& buffer, const DrawIndirectType type, const Stride stride) :
            Buffer(std::move(buffer)), m_Type(type), m_Stride(stride)
        {}
    } // namespace rhi
} // namespace vultra