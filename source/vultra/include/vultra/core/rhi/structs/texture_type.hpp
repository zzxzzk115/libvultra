#pragma once

namespace vultra
{
    namespace rhi
    {
        enum class TextureType
        {
            eUndefined,
            eTexture1D,
            eTexture1DArray,
            eTexture2D,
            eTexture2DArray,
            eTexture3D,
            eTextureCube,
            eTextureCubeArray,
        };
    } // namespace rhi
} // namespace vultra
