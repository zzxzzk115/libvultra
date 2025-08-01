#pragma once

#include "vultra/core/rhi/buffer.hpp"

namespace vultra
{
    namespace rhi
    {
        class VertexBuffer final : public Buffer
        {
            friend class RenderDevice;

        public:
            VertexBuffer() = default;

            [[nodiscard]] Stride         getStride() const;
            [[nodiscard]] vk::DeviceSize getCapacity() const;

        private:
            VertexBuffer(Buffer&&, Stride);

        private:
            Stride m_Stride {0};
        };
    } // namespace rhi
} // namespace vultra