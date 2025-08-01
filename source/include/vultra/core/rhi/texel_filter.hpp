#pragma once

#include <vulkan/vulkan.hpp>

namespace vultra
{
    namespace rhi
    {
        // https://registry.khronos.org/vulkan/specs/1.3/html/chap13.html#VkFilter
        enum class TexelFilter
        {
            eNearest = VK_FILTER_NEAREST,
            eLinear  = VK_FILTER_LINEAR,
        };
    } // namespace rhi
} // namespace vultra