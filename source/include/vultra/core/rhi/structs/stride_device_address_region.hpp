#pragma once

#include <cstdint>

namespace vultra
{
    namespace rhi
    {
        struct StrideDeviceAddressRegion
        {
            uint64_t deviceAddress {0};
            uint64_t stride {0};
            uint64_t size {0};
        };
    } // namespace rhi
} // namespace vultra
