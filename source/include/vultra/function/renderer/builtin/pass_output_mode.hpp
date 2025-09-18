#pragma once

namespace vultra
{
    namespace gfx
    {
        // NOLINTBEGIN
        enum class PassOutputMode
        {
            Albedo,
            Normal,
            Emissive,
            Metallic,
            Roughness,
            AmbientOcclusion,
            Depth,
            SceneColor_HDR,
            SceneColor_LDR,
        };
        // NOLINTEND
    } // namespace gfx
} // namespace vultra