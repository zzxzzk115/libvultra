#pragma once

#include <cstdint>
#include <limits>

namespace vultra::resource
{
    struct CpuAsset
    {
        uint32_t index {std::numeric_limits<uint32_t>::max()};
    };
} // namespace vultra::resource
