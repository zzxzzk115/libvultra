#include "vultra/core/rhi/backends/vk/vulkan_acceleration_structure.hpp"
#include "vultra/core/rhi/backends/vk/handle_utils.hpp"

namespace vultra
{
    namespace rhi
    {
        VulkanAccelerationStructure::VulkanAccelerationStructure(const vk::Device                      device,
                                                                 const vk::AccelerationStructureKHR    handle,
                                                                 const DeviceAddress                   deviceAddress,
                                                                 const AccelerationStructureType       type,
                                                                 AccelerationStructureBuildSizesInfo&& buildSizesInfo,
                                                                 AccelerationStructureBuffer&&         buffer) :
            m_Device(device), m_Handle(handle), m_DeviceAddress(deviceAddress),
            m_BuildSizesInfo(std::move(buildSizesInfo)), m_Type(type), m_Buffer(std::move(buffer))
        {}

        VulkanAccelerationStructure::~VulkanAccelerationStructure() { destroy(); }

        bool VulkanAccelerationStructure::isValid() const { return m_Handle != nullptr; }

        std::uintptr_t VulkanAccelerationStructure::getHandle() const
        {
            return toBackendHandle(static_cast<VkAccelerationStructureKHR>(m_Handle));
        }

        DeviceAddress VulkanAccelerationStructure::getDeviceAddress() const { return m_DeviceAddress; }

        AccelerationStructureBuildSizesInfo VulkanAccelerationStructure::getBuildSizesInfo() const
        {
            return m_BuildSizesInfo;
        }

        AccelerationStructureType VulkanAccelerationStructure::getType() const { return m_Type; }

        AccelerationStructureBuffer* VulkanAccelerationStructure::getBuffer() { return &m_Buffer; }

        void VulkanAccelerationStructure::destroy() noexcept
        {
            if (m_Device && m_Handle)
            {
                m_Device.destroyAccelerationStructureKHR(m_Handle);
                m_Handle = nullptr;
            }
        }
    } // namespace rhi
} // namespace vultra
