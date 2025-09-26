#pragma once

#include <vulkan/vulkan.hpp>

namespace vultra
{
    namespace rhi
    {
        enum class AccelerationStructureType
        {
            eTopLevel    = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
            eBottomLevel = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
            eGeneric     = VK_ACCELERATION_STRUCTURE_TYPE_GENERIC_KHR
        };
    }
} // namespace vultra