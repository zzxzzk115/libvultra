#pragma once

#include "vultra/core/rhi/structs/draw_indirect_type.hpp"

#include <cstdint>

namespace vultra
{
    namespace rhi
    {
        struct DrawIndirectCommand
        {
            DrawIndirectType type {DrawIndirectType::eNonIndexed};
            uint32_t         count {0};
            uint32_t         instanceCount {1};
            uint32_t         first {0};
            int32_t          vertexOffset {0};
            uint32_t         firstInstance {0};
        };
    } // namespace rhi
} // namespace vultra
