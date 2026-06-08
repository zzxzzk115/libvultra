#pragma once

#define VULTRA_BUILTIN_RESOURCE_CITRUS_ORCHARD_SKY_TEXTURE 1001
#define VULTRA_BUILTIN_RESOURCE_PACK 1002

#ifndef RC_INVOKED
namespace vultra
{
    inline constexpr int kBuiltinResourceCitrusOrchardSkyTexture =
        VULTRA_BUILTIN_RESOURCE_CITRUS_ORCHARD_SKY_TEXTURE;

    // The embedded builtin.vpk (shaders/fonts/textures/render graphs). Present only in
    // self-contained binaries (editor, examples) that add the vultra.builtin_pack rule.
    inline constexpr int kBuiltinResourcePack = VULTRA_BUILTIN_RESOURCE_PACK;
}
#endif
