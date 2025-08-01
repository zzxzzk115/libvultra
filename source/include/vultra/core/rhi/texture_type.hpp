#pragma once

#include <string_view>

namespace vultra
{
    namespace rhi
    {
        enum class TextureType
        {
            eUndefined = 0,

            eTexture1D,
            eTexture1DArray,
            eTexture2D,
            eTexture2DArray,
            eTexture3D,
            eTextureCube,
            eTextureCubeArray,
        };

        [[nodiscard]] std::string_view toString(TextureType);
    } // namespace rhi
} // namespace vultra