#pragma once

#include "vultra/core/rhi/structs/device_address.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace vultra
{
    namespace rhi
    {
        struct GeometryNode
        {
            DeviceAddress vertexBufferDeviceAddress;
            DeviceAddress indexBufferDeviceAddress;

            // Name -> Material ID
            std::unordered_map<std::string, uint32_t> materials;
        };
    } // namespace rhi
} // namespace vultra
