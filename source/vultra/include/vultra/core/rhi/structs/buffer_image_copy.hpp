#pragma once

#include "vultra/core/rhi/structs/image_aspect.hpp"

#include <cstdint>

namespace vultra
{
    namespace rhi
    {
        struct BufferImageCopy
        {
            uint64_t bufferOffset {0};
            uint32_t bufferRowLength {0};
            uint32_t bufferImageHeight {0};

            ImageAspectFlags aspectMask {ImageAspectFlags::eColor};
            uint32_t         mipLevel {0};
            uint32_t         baseArrayLayer {0};
            uint32_t         layerCount {1};

            int32_t imageOffsetX {0};
            int32_t imageOffsetY {0};
            int32_t imageOffsetZ {0};

            uint32_t imageExtentWidth {0};
            uint32_t imageExtentHeight {0};
            uint32_t imageExtentDepth {1};
        };
    } // namespace rhi
} // namespace vultra
