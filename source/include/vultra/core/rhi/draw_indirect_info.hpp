#pragma once

#include "vultra/core/rhi/draw_indirect_buffer.hpp"

namespace vultra
{
    namespace rhi
    {
        struct DrawIndirectInfo
        {
            const DrawIndirectBuffer* buffer {nullptr};

            uint32_t firstCommand {0}; // offset in command buffer
            uint32_t commandCount {0}; // number of draws
        };
    } // namespace rhi
} // namespace vultra