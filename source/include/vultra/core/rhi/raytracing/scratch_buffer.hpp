#pragma once

#include "vultra/core/rhi/buffer.hpp"

namespace vultra
{
    namespace rhi
    {
        class ScratchBuffer : public Buffer
        {
        public:
            ScratchBuffer() = default;

            uint64_t getDeviceAddress() const;

        private:
            friend class RenderDevice;
            ScratchBuffer(Buffer&& buffer, uint64_t deviceAddress);

            uint64_t m_DeviceAddress {0};
        };
    } // namespace rhi
} // namespace vultra