#pragma once

namespace vultra
{
    namespace gfx
    {
        // NOLINTBEGIN
        enum class PassOutputMode
        {
            GBuffer_Albedo,
            GBuffer_Normal,
            GBuffer_Emissive,
            GBuffer_MetallicRoughnessAO,
            GBuffer_Depth,
            SceneColor_HDR,
            SceneColor_LDR,
        };
        // NOLINTEND
    } // namespace gfx
} // namespace vultra