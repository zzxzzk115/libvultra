#pragma once

#include "vultra/core/rhi/resource_indices.hpp"

#include <vulkan/vulkan.hpp>

#include <map>

namespace vultra
{
    namespace rhi
    {
        struct VertexAttribute
        {
            enum class Type
            {
                eFloat  = VK_FORMAT_R32_SFLOAT,
                eFloat2 = VK_FORMAT_R32G32_SFLOAT,
                eFloat3 = VK_FORMAT_R32G32B32_SFLOAT,
                eFloat4 = VK_FORMAT_R32G32B32A32_SFLOAT,

                eInt4 = VK_FORMAT_R32G32B32A32_SINT,

                eUByte4_Norm = VK_FORMAT_R8G8B8A8_UNORM,
            };

            Type     type;
            uint32_t offset {0};
        };

        // layout(location = index)
        using VertexAttributes = std::map<LocationIndex, VertexAttribute>;

        [[nodiscard]] uint32_t getSize(const VertexAttribute::Type);
    } // namespace rhi
} // namespace vultra