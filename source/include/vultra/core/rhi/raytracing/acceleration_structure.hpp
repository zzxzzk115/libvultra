#pragma once

#include "vultra/core/rhi/raytracing/acceleration_structure_build_sizes_info.hpp"
#include "vultra/core/rhi/raytracing/acceleration_structure_type.hpp"
#include "vultra/core/rhi/raytracing/buffer_define.hpp"

namespace vultra
{
    namespace rhi
    {
        struct AccelerationStructure
        {
        public:
            AccelerationStructure()                             = default;
            AccelerationStructure(const AccelerationStructure&) = delete;
            AccelerationStructure(AccelerationStructure&&) noexcept;
            ~AccelerationStructure();

            AccelerationStructure& operator=(const AccelerationStructure&) = delete;
            AccelerationStructure& operator=(AccelerationStructure&&) noexcept;

            [[nodiscard]] explicit operator bool() const;

            [[nodiscard]] vk::AccelerationStructureKHR getHandle() const;

            [[nodiscard]] uint64_t getDeviceAddress() const;

            [[nodiscard]] AccelerationStructureBuildSizesInfo getBuildSizesInfo() const;

            [[nodiscard]] AccelerationStructureType getType() const;

            [[nodiscard]] AccelerationStructureBuffer* getBuffer();

        private:
            friend class RenderDevice;
            AccelerationStructure(vk::Device,
                                  vk::AccelerationStructureKHR,
                                  uint64_t,
                                  AccelerationStructureType,
                                  AccelerationStructureBuildSizesInfo&&,
                                  AccelerationStructureBuffer&&);

            void destroy();

            vk::Device                   m_Device {nullptr};
            vk::AccelerationStructureKHR m_Handle {nullptr};
            uint64_t                     m_DeviceAddress {0};

            AccelerationStructureBuildSizesInfo m_BuildSizesInfo {};
            AccelerationStructureType           m_Type {AccelerationStructureType::eTopLevel};
            AccelerationStructureBuffer         m_Buffer;
        };
    } // namespace rhi
} // namespace vultra