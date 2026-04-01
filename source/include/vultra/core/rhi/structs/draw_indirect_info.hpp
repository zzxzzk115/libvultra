#pragma once

#include "vultra/core/rhi/structs/draw_indirect_type.hpp"
#include "vultra/core/rhi/structs/geometry_info.hpp"

#include <cstdint>

namespace vultra
{
    namespace rhi
    {
        class DrawIndirectBuffer;

        struct DrawIndirectInfo
        {
            const DrawIndirectBuffer* buffer {nullptr};
            uint32_t                  firstCommand {0};
            uint32_t                  commandCount {0};
            GeometryInfo              gi {};
        };
    } // namespace rhi
} // namespace vultra
