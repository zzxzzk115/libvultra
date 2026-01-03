#include "vultra/core/rhi/draw_indirect_buffer.hpp"
#include "vultra/core/rhi/draw_indirect_type.hpp"

#include <vulkan/vulkan_structs.hpp>

namespace vultra
{
    namespace rhi
    {
        DrawIndirectType DrawIndirectBuffer::getDrawIndirectType() const { return m_Type; }

        Buffer::Stride DrawIndirectBuffer::getStride() const
        {
            return m_Type == DrawIndirectType::eIndexed ? sizeof(vk::DrawIndexedIndirectCommand) :
                                                          sizeof(vk::DrawIndirectCommand);
        }

        vk::DeviceSize DrawIndirectBuffer::getCapacity() const { return getStride() > 0 ? getSize() / getStride() : 0; }

        DrawIndirectBuffer::DrawIndirectBuffer(Buffer&& buffer, const DrawIndirectType type) :
            Buffer(std::move(buffer)), m_Type(type)
        {}
    } // namespace rhi
} // namespace vultra