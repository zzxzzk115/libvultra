#include "vultra/core/rhi/raytracing/scratch_buffer.hpp"

namespace vultra
{
    namespace rhi
    {
        ScratchBuffer::ScratchBuffer(Buffer&& buffer, uint64_t deviceAddress) :
            Buffer(std::move(buffer)), m_DeviceAddress(deviceAddress)
        {}

        uint64_t ScratchBuffer::getDeviceAddress() const { return m_DeviceAddress; }
    } // namespace rhi
} // namespace vultra