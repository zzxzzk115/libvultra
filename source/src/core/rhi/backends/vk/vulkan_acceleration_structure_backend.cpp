#include "vultra/core/rhi/backends/vk/vulkan_acceleration_structure_backend.hpp"

namespace vultra
{
    namespace rhi
    {
        VulkanAccelerationStructureBackend::VulkanAccelerationStructureBackend(
            const vk::Device                            device,
            const vk::AccelerationStructureKHR          handle,
            const DeviceAddress                         deviceAddress,
            const AccelerationStructureType             type,
            AccelerationStructureBuildSizesInfo&&       buildSizesInfo,
            AccelerationStructureBuffer&&               buffer) :
            m_Device(device), m_Handle(handle), m_DeviceAddress(deviceAddress), m_BuildSizesInfo(std::move(buildSizesInfo)),
            m_Type(type), m_Buffer(std::move(buffer))
        {}

        VulkanAccelerationStructureBackend::~VulkanAccelerationStructureBackend() { destroy(); }

        bool VulkanAccelerationStructureBackend::isValid() const { return m_Handle != nullptr; }

        std::uintptr_t VulkanAccelerationStructureBackend::getHandle() const
        {
            return reinterpret_cast<std::uintptr_t>(static_cast<VkAccelerationStructureKHR>(m_Handle));
        }

        DeviceAddress VulkanAccelerationStructureBackend::getDeviceAddress() const { return m_DeviceAddress; }

        AccelerationStructureBuildSizesInfo VulkanAccelerationStructureBackend::getBuildSizesInfo() const
        {
            return m_BuildSizesInfo;
        }

        AccelerationStructureType VulkanAccelerationStructureBackend::getType() const { return m_Type; }

        AccelerationStructureBuffer* VulkanAccelerationStructureBackend::getBuffer() { return &m_Buffer; }

        void VulkanAccelerationStructureBackend::destroy() noexcept
        {
            if (m_Device && m_Handle)
            {
                m_Device.destroyAccelerationStructureKHR(m_Handle);
                m_Handle = nullptr;
            }
        }
    } // namespace rhi
} // namespace vultra
