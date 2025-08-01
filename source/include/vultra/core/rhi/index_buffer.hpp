#pragma once

#include "vultra/core/rhi/buffer.hpp"

namespace vultra
{
    namespace rhi
    {
        enum class IndexType
        {
            eUndefined = 0,
            eUInt16    = 2,
            eUInt32    = 4
        };

        class IndexBuffer final : public Buffer
        {
            friend class RenderDevice;

        public:
            IndexBuffer() = default;

            [[nodiscard]] IndexType      getIndexType() const;
            [[nodiscard]] Stride         getStride() const;
            [[nodiscard]] vk::DeviceSize getCapacity() const;

        private:
            IndexBuffer(Buffer&&, IndexType);

        private:
            IndexType m_IndexType {IndexType::eUndefined};
        };
    } // namespace rhi
} // namespace vultra