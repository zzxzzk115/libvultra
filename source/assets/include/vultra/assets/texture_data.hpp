#pragma once

#include <vultra/assets/texture_format.hpp>
#include <vultra/core/math/extent.hpp>

#include <cstddef>
#include <vector>

namespace vultra
{
    struct TextureLevel
    {
        Extent                 size;
        std::vector<std::byte> bytes;
    };

    struct TextureData
    {
        TextureFormat             format = TextureFormat::eUnknown;
        std::vector<TextureLevel> levels;
        // Import provenance: leading authored mips are restored from the source, never cached.
        int      sourceImage  = -1;
        uint32_t sourceMipNum = 0;
    };

} // namespace vultra
