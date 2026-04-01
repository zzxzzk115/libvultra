#pragma once

#include <cstdint>

namespace vultra
{
    namespace rhi
    {
        struct BufferCopy
        {
            uint64_t srcOffset {0};
            uint64_t dstOffset {0};
            uint64_t size {0};
        };
    } // namespace rhi
} // namespace vultra
