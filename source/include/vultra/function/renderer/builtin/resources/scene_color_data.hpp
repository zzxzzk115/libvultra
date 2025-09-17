#pragma once

#include <fg/Fwd.hpp>

namespace vultra
{
    namespace gfx
    {
        struct SceneColorData
        {
            FrameGraphResource ldr;
            FrameGraphResource hdr;
            FrameGraphResource bright;
            FrameGraphResource aa;
        };
    } // namespace gfx
} // namespace vultra