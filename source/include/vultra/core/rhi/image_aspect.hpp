#pragma once

namespace vultra
{
    namespace rhi
    {
        enum class ImageAspect
        {
            eNone = 0,
            eColor,
            eDepth,
            eStencil
        };
    }
} // namespace vultra