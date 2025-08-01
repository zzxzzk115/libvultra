#include "vultra/core/rhi/index_buffer.hpp"

namespace vultra
{
    namespace rhi
    {
        IndexType IndexBuffer::getIndexType() const { return m_IndexType; }

        Buffer::Stride IndexBuffer::getStride() const { return std::to_underlying(m_IndexType); }

        vk::DeviceSize IndexBuffer::getCapacity() const
        {
            assert(m_IndexType != IndexType::eUndefined);

            const auto stride = static_cast<int32_t>(m_IndexType);
            return stride > 0 ? getSize() / stride : 0;
        }

        IndexBuffer::IndexBuffer(Buffer&& buffer, const IndexType indexType) :
            Buffer(std::move(buffer)), m_IndexType(indexType)
        {}
    } // namespace rhi
} // namespace vultra