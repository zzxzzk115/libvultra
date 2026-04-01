#pragma once

#include "vultra/core/rhi/structs/frame_index.hpp"

#include <cstdint>

namespace vultra
{
    namespace rhi
    {
        struct RenderTargetView
        {
            FrameIndex     frameIndex {};
            std::uintptr_t handle {0};
        };
    } // namespace rhi
} // namespace vultra
