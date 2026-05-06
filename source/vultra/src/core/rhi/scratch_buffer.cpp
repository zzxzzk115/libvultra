#include "vultra/core/rhi/scratch_buffer.hpp"

namespace vultra
{
    namespace rhi
    {
        ScratchBuffer::ScratchBuffer(Buffer&& buffer, DeviceAddress deviceAddress) :
            Buffer(std::move(buffer)), m_DeviceAddress(deviceAddress)
        {}

        DeviceAddress ScratchBuffer::getDeviceAddress() const { return m_DeviceAddress; }
    } // namespace rhi
} // namespace vultra
