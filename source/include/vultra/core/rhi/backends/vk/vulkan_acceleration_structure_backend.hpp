#pragma once

#include "vultra/core/rhi/interfaces/iacceleration_structure_backend.hpp"

#include <vulkan/vulkan.hpp>

namespace vultra
{
    namespace rhi
    {
        class VulkanAccelerationStructureBackend final : public IAccelerationStructureBackend
        {
        public:
            VulkanAccelerationStructureBackend(vk::Device                            device,
                                               vk::AccelerationStructureKHR          handle,
                                               DeviceAddress                         deviceAddress,
                                               AccelerationStructureType             type,
                                               AccelerationStructureBuildSizesInfo&& buildSizesInfo,
                                               AccelerationStructureBuffer&&         buffer);
            ~VulkanAccelerationStructureBackend() override;

            VulkanAccelerationStructureBackend(const VulkanAccelerationStructureBackend&) = delete;
            VulkanAccelerationStructureBackend(VulkanAccelerationStructureBackend&&) noexcept = delete;
            VulkanAccelerationStructureBackend& operator=(const VulkanAccelerationStructureBackend&) = delete;
            VulkanAccelerationStructureBackend& operator=(VulkanAccelerationStructureBackend&&) noexcept = delete;

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
