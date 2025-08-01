#pragma once

#include <vulkan/vulkan.hpp>

namespace vultra
{
    namespace rhi
    {
        // https://registry.khronos.org/vulkan/specs/1.3/html/chap25.html#VkCullModeFlagBits
        enum class CullMode : VkCullModeFlags
        {
            // Specifies that no triangles are discarded
            eNone = VK_CULL_MODE_NONE,
            // Specifies that front-facing triangles are discarded
            eFront = VK_CULL_MODE_FRONT_BIT,
            // Specifies that back-facing triangles are discarded
            eBack = VK_CULL_MODE_BACK_BIT,
        };

        [[nodiscard]] std::string_view toString(const CullMode);
    } // namespace rhi
} // namespace vultra