#pragma once

#include <cstdint>

#include "vultra/core/rhi/structs/buffer_usage.hpp"

namespace vultra
{
    namespace rhi
    {
        struct RadixSorterStorageRequirements
        {
            uint64_t    size {0};
            BufferUsage usage {BufferUsage::eStorageBuffer};
        };
    } // namespace rhi
} // namespace vultra
