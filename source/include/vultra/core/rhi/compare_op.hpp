#pragma once

#include <vulkan/vulkan.hpp>

namespace vultra
{
    namespace rhi
    {
        // https://registry.khronos.org/vulkan/specs/1.3/html/chap13.html#VkCompareOp
        enum class CompareOp
        {
            eNever          = VK_COMPARE_OP_NEVER,
            eLess           = VK_COMPARE_OP_LESS,
            eEqual          = VK_COMPARE_OP_EQUAL,
            eLessOrEqual    = VK_COMPARE_OP_LESS_OR_EQUAL,
            eGreater        = VK_COMPARE_OP_GREATER,
            eNotEqual       = VK_COMPARE_OP_NOT_EQUAL,
            eGreaterOrEqual = VK_COMPARE_OP_GREATER_OR_EQUAL,
            eAlways         = VK_COMPARE_OP_ALWAYS,
        };
    } // namespace rhi
} // namespace vultra