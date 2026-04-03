#pragma once

#include "vultra/core/rhi/buffer.hpp"
#include "vultra/core/rhi/structs/device_address.hpp"

namespace vultra
{
    namespace rhi
    {
        class ScratchBuffer : public Buffer
        {
        public:
            ScratchBuffer() = default;

            DeviceAddress getDeviceAddress() const;

        private:
            friend class RenderDevice;
            ScratchBuffer(Buffer&& buffer, DeviceAddress deviceAddress);

            DeviceAddress m_DeviceAddress;
        };
    } // namespace rhi
} // namespace vultra
