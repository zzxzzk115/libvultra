#pragma once

#include "vultra/core/rhi/buffer.hpp"
#include "vultra/core/rhi/structs/index_type.hpp"

namespace vultra
{
    namespace rhi
    {
        class IndexBuffer final : public Buffer
        {
            friend class RenderDevice;

        public:
            IndexBuffer() = default;

            [[nodiscard]] IndexType      getIndexType() const;
            [[nodiscard]] Stride         getStride() const;
            [[nodiscard]] uint64_t       getCapacity() const;

        private:
            IndexBuffer(Buffer&&, IndexType);

        private:
            IndexType m_IndexType {IndexType::eUndefined};
        };
    } // namespace rhi
} // namespace vultra
