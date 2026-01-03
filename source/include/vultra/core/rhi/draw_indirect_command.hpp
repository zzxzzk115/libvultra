#pragma once

#include "vultra/core/rhi/draw_indirect_type.hpp"

namespace vultra
{
    namespace rhi
    {
        struct DrawIndirectCommand
        {
            DrawIndirectType type;

            uint32_t count {0}; // vertexCount / indexCount
            uint32_t instanceCount {1};

            uint32_t first {0};        // firstVertex / firstIndex
            int32_t  vertexOffset {0}; // only for indexed
            uint32_t firstInstance {0};
        };
    } // namespace rhi
} // namespace vultra