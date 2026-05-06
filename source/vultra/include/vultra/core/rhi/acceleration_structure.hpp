#pragma once

#include "vultra/core/rhi/interfaces/iacceleration_structure.hpp"
#include "vultra/core/rhi/structs/acceleration_structure_build_sizes_info.hpp"
#include "vultra/core/rhi/structs/acceleration_structure_type.hpp"
#include "vultra/core/rhi/structs/device_address.hpp"
#include "vultra/core/rhi/structs/raytracing_buffer_aliases.hpp"

#include <cstdint>
#include <memory>

namespace vultra
{
    namespace rhi
    {
        struct AccelerationStructure
        {
        public:
            AccelerationStructure()                                 = default;
            AccelerationStructure(const AccelerationStructure&)     = delete;
            AccelerationStructure(AccelerationStructure&&) noexcept = default;
            ~AccelerationStructure()                                = default;

            AccelerationStructure& operator=(const AccelerationStructure&)     = delete;
            AccelerationStructure& operator=(AccelerationStructure&&) noexcept = default;

            [[nodiscard]] explicit operator bool() const;

            [[nodiscard]] std::uintptr_t getHandle() const;

            [[nodiscard]] DeviceAddress getDeviceAddress() const;

            [[nodiscard]] AccelerationStructureBuildSizesInfo getBuildSizesInfo() const;

            [[nodiscard]] AccelerationStructureType getType() const;

            [[nodiscard]] AccelerationStructureBuffer* getBuffer();

        private:
            friend class RenderDevice;
            explicit AccelerationStructure(std::unique_ptr<IAccelerationStructure>);

            std::unique_ptr<IAccelerationStructure> m_Backend;
        };
    } // namespace rhi
} // namespace vultra
