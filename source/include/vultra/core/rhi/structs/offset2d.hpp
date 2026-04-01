#pragma once

#include <vulkan/vulkan.hpp>

namespace vultra
{
    namespace rhi
    {
        struct Offset2D
        {
            int32_t x {0};
            int32_t y {0};

            [[nodiscard]] explicit operator vk::Offset2D() const { return {x, y}; }

            auto operator<=>(const Offset2D&) const = default;
        };
    } // namespace rhi
} // namespace vultra
