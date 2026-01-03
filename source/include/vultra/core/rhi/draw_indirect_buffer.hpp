#pragma once

#include "vultra/core/rhi/buffer.hpp"
#include "vultra/core/rhi/draw_indirect_type.hpp"

namespace vultra
{
    namespace rhi
    {
        class DrawIndirectBuffer final : public Buffer
        {
            friend class RenderDevice;

        public:
            DrawIndirectBuffer() = default;

            [[nodiscard]] DrawIndirectType getDrawIndirectType() const;
            [[nodiscard]] Stride           getStride() const;
            [[nodiscard]] vk::DeviceSize   getCapacity() const;

        private:
            DrawIndirectBuffer(Buffer&&, DrawIndirectType);

        private:
            DrawIndirectType m_Type {DrawIndirectType::eIndexed};
        };
    } // namespace rhi
} // namespace vultra