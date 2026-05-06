#pragma once

#include "vultra/core/rhi/interfaces/iacceleration_structure.hpp"

#include <vulkan/vulkan.hpp>

namespace vultra
{
    namespace rhi
    {
        class VulkanAccelerationStructure final : public IAccelerationStructure
        {
        public:
            VulkanAccelerationStructure(vk::Device                            device,
                                               vk::AccelerationStructureKHR          handle,
                                               DeviceAddress                         deviceAddress,
                                               AccelerationStructureType             type,
                                               AccelerationStructureBuildSizesInfo&& buildSizesInfo,
                                               AccelerationStructureBuffer&&         buffer);
            ~VulkanAccelerationStructure() override;

            VulkanAccelerationStructure(const VulkanAccelerationStructure&) = delete;
            VulkanAccelerationStructure(VulkanAccelerationStructure&&) noexcept = delete;
            VulkanAccelerationStructure& operator=(const VulkanAccelerationStructure&) = delete;
            VulkanAccelerationStructure& operator=(VulkanAccelerationStructure&&) noexcept = delete;

            [[nodiscard]] bool                               isValid() const override;
            [[nodiscard]] std::uintptr_t                     getHandle() const override;
            [[nodiscard]] DeviceAddress                      getDeviceAddress() const override;
            [[nodiscard]] AccelerationStructureBuildSizesInfo getBuildSizesInfo() const override;
            [[nodiscard]] AccelerationStructureType           getType() const override;
            [[nodiscard]] AccelerationStructureBuffer*        getBuffer() override;

        private:
            void destroy() noexcept;

        private:
            vk::Device                   m_Device {nullptr};
            vk::AccelerationStructureKHR m_Handle {nullptr};
            DeviceAddress                m_DeviceAddress {};
            AccelerationStructureBuildSizesInfo m_BuildSizesInfo {};
            AccelerationStructureType           m_Type {AccelerationStructureType::eTopLevel};
            AccelerationStructureBuffer         m_Buffer;
        };
    } // namespace rhi
} // namespace vultra
