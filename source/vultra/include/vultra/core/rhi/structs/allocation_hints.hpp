#pragma once

#include "vultra/core/base/base.hpp"

namespace vultra
{
    namespace rhi
    {
        enum class AllocationHints
        {
            eNone            = ZERO_BIT,
            eMinMemory       = BIT(0),
            eSequentialWrite = BIT(1),
            eRandomAccess    = BIT(2),
        };
    } // namespace rhi
} // namespace vultra
