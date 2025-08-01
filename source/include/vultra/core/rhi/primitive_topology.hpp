#pragma once

#include <vulkan/vulkan.hpp>

namespace vultra
{
    namespace rhi
    {
        // https://registry.khronos.org/vulkan/specs/1.3/html/chap20.html#VkPrimitiveTopology
        enum class PrimitiveTopology
        {
            ePointList     = VK_PRIMITIVE_TOPOLOGY_POINT_LIST,
            eLineList      = VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
            eTriangleList  = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            eTriangleStrip = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
            eTriangleFan   = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN,
        };

        [[nodiscard]] std::string_view toString(const PrimitiveTopology);
    } // namespace rhi
} // namespace vultra