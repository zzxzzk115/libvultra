#pragma once

#include "vultra/core/rhi/structs/raytracing_buffer_aliases.hpp"
#include "vultra/core/rhi/structs/acceleration_structure_build_sizes_info.hpp"
#include "vultra/core/rhi/structs/acceleration_structure_type.hpp"
#include "vultra/core/rhi/structs/device_address.hpp"

#include <cstdint>

namespace vultra
{
    namespace rhi
    {
        class IAccelerationStructure
        {
        public:
            virtual ~IAccelerationStructure() = default;

            [[nodiscard]] virtual bool isValid() const = 0;
            [[nodiscard]] virtual std::uintptr_t                   getHandle() const = 0;
            [[nodiscard]] virtual DeviceAddress                    getDeviceAddress() const = 0;
            [[nodiscard]] virtual AccelerationStructureBuildSizesInfo getBuildSizesInfo() const = 0;
            [[nodiscard]] virtual AccelerationStructureType           getType() const = 0;
            [[nodiscard]] virtual AccelerationStructureBuffer*        getBuffer() = 0;
        };
    } // namespace rhi
} // namespace vultra
