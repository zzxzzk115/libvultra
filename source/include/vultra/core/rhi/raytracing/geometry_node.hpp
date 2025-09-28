#pragma once

#include <string>
#include <unordered_map>

namespace vultra
{
    namespace rhi
    {
        struct GeometryNode
        {
            uint64_t vertexBufferDeviceAddress {0};
            uint64_t indexBufferDeviceAddress {0};

            // Name -> Material ID
            std::unordered_map<std::string, uint32_t> materials;
        };
    } // namespace rhi
} // namespace vultra