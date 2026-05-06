#pragma once

#include "vultra/core/rhi/structs/device_address.hpp"

#include <cstdint>

namespace vultra
{
    namespace rhi
    {
        struct StrideDeviceAddressRegion
        {
            DeviceAddress deviceAddress;
            uint64_t      stride {0};
            uint64_t      size {0};
        };
    } // namespace rhi
} // namespace vultra
