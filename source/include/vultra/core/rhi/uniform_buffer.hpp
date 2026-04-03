#pragma once

#include "vultra/core/rhi/buffer.hpp"

namespace vultra
{
    namespace rhi
    {
        class UniformBuffer final : public Buffer
        {
            friend class RenderDevice;

        public:
            UniformBuffer() = default;

            [[nodiscard]] uint64_t getCapacity() const;

        private:
            explicit UniformBuffer(Buffer&&);
        };
    }
} // namespace vultra
