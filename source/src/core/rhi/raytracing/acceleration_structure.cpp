#include "vultra/core/rhi/raytracing/acceleration_structure.hpp"

namespace vultra
{
    namespace rhi
    {
        AccelerationStructure::AccelerationStructure(AccelerationStructure&& other) noexcept :
            m_Device(other.m_Device), m_Handle(other.m_Handle), m_DeviceAddress(other.m_DeviceAddress),
            m_BuildSizesInfo(std::move(other.m_BuildSizesInfo)), m_Type(other.m_Type),
            m_Buffer(std::move(other.m_Buffer))
        {
            other.m_Device         = nullptr;
            other.m_Handle         = nullptr;
            other.m_DeviceAddress  = 0;
            other.m_Type           = AccelerationStructureType::eTopLevel;
            other.m_BuildSizesInfo = {};
        }

        AccelerationStructure::~AccelerationStructure() { destroy(); }

        AccelerationStructure& AccelerationStructure::operator=(AccelerationStructure&& rhs) noexcept
        {
            if (this != &rhs)
            {
                destroy();

                std::swap(m_Device, rhs.m_Device);
                std::swap(m_Handle, rhs.m_Handle);
                std::swap(m_DeviceAddress, rhs.m_DeviceAddress);
                std::swap(m_Type, rhs.m_Type);

                m_BuildSizesInfo     = std::move(rhs.m_BuildSizesInfo);
                rhs.m_BuildSizesInfo = {};

                m_Buffer     = std::move(rhs.m_Buffer);
                rhs.m_Buffer = {};
            }

            return *this;
        }

        AccelerationStructure::operator bool() const { return m_Handle != nullptr; }

        vk::AccelerationStructureKHR AccelerationStructure::getHandle() const { return m_Handle; }

        uint64_t AccelerationStructure::getDeviceAddress() const { return m_DeviceAddress; }

        AccelerationStructureBuildSizesInfo AccelerationStructure::getBuildSizesInfo() const
        {
            return m_BuildSizesInfo;
        }

        AccelerationStructureType AccelerationStructure::getType() const { return m_Type; }

        AccelerationStructureBuffer* AccelerationStructure::getBuffer() { return &m_Buffer; }

        AccelerationStructure::AccelerationStructure(vk::Device                            deviceHandle,
                                                     vk::AccelerationStructureKHR          handle,
                                                     uint64_t                              deviceAddress,
                                                     AccelerationStructureType             type,
                                                     AccelerationStructureBuildSizesInfo&& buildSizesInfo,
                                                     AccelerationStructureBuffer&&         buffer) :
            m_Device(deviceHandle), m_Handle(handle), m_DeviceAddress(deviceAddress), m_Type(type),
            m_BuildSizesInfo(std::move(buildSizesInfo)), m_Buffer(std::move(buffer))
        {}

        void AccelerationStructure::destroy()
        {
            if (m_Device && m_Handle)
            {
                m_Device.destroyAccelerationStructureKHR(m_Handle);
                m_Handle = nullptr;
            }
        }
    } // namespace rhi
} // namespace vultra