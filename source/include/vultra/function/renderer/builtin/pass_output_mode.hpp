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
            TextureLodDebug,
            SceneColor_HDR,
            SceneColor_LDR,
            SceneColor_AntiAliased,
        };
        // NOLINTEND
    } // namespace gfx
} // namespace vultra