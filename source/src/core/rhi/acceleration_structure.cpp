#include "vultra/core/rhi/acceleration_structure.hpp"

namespace vultra
{
    namespace rhi
    {
        AccelerationStructure::operator bool() const
        {
            return m_Backend && m_Backend->isValid();
        }

        std::uintptr_t AccelerationStructure::getHandle() const
        {
            assert(m_Backend);
            return m_Backend->getHandle();
        }

        DeviceAddress AccelerationStructure::getDeviceAddress() const
        {
            assert(m_Backend);
            return m_Backend->getDeviceAddress();
        }

        AccelerationStructureBuildSizesInfo AccelerationStructure::getBuildSizesInfo() const
        {
            assert(m_Backend);
            return m_Backend->getBuildSizesInfo();
        }

        AccelerationStructureType AccelerationStructure::getType() const
        {
            assert(m_Backend);
            return m_Backend->getType();
        }

        AccelerationStructureBuffer* AccelerationStructure::getBuffer()
        {
            assert(m_Backend);
            return m_Backend->getBuffer();
        }

        AccelerationStructure::AccelerationStructure(std::unique_ptr<IAccelerationStructure> backend) :
            m_Backend(std::move(backend))
        {}
    } // namespace rhi
} // namespace vultra
