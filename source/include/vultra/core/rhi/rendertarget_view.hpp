#pragma once

#include "vultra/core/rhi/frame_index.hpp"

namespace vultra
{
    namespace rhi
    {
        class Texture;

        struct RenderTargetView
        {
            const FrameIndex::ValueType frameIndex {0}; // Image in flight index.
            Texture&                    texture;
        };
    } // namespace rhi
} // namespace vultra