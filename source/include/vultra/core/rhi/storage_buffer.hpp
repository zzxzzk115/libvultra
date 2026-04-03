#pragma once

#include "vultra/core/rhi/buffer.hpp"

namespace vultra
{
    namespace rhi
    {
        class StorageBuffer final : public Buffer
        {
            friend class RenderDevice;

        public:
            StorageBuffer() = default;

            [[nodiscard]] uint64_t getCapacity() const;

        private:
            explicit StorageBuffer(Buffer&&);
        };
    }
} // namespace vultra
