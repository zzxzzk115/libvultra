#pragma once

#include <cstdint>

namespace vultra
{
    namespace rhi
    {
        enum class DrawIndirectType : uint8_t
        {
            eNonIndexed,
            eIndexed
        };
    } // namespace rhi
} // namespace vultra