#pragma once

#include <compare>

namespace vultra
{
    namespace rhi
    {
        struct Offset2D
        {
            int32_t x {0};
            int32_t y {0};

            auto operator<=>(const Offset2D&) const = default;
        };
    } // namespace rhi
} // namespace vultra
