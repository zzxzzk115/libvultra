#pragma once

#include <cstdint>

namespace vultra
{
    struct Extent
    {
        uint32_t width                           = 0;
        uint32_t height                          = 0;
        bool     operator==(const Extent&) const = default;

        bool empty() const
        {
            return width == 0 || height == 0;
        }
    };
} // namespace vultra
