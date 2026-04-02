#pragma once

#include <cstdint>

namespace vultra
{
    namespace rhi
    {
        // GPU virtual address used by features such as ray tracing / shader device address.
        // This is not a generic backend object handle.
        struct DeviceAddress
        {
            uint64_t value {0};

            constexpr DeviceAddress() = default;
            constexpr explicit DeviceAddress(uint64_t v) : value(v) {}

            [[nodiscard]] constexpr explicit operator bool() const { return value != 0; }
        };
    } // namespace rhi
} // namespace vultra
