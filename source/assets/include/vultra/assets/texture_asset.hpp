#pragma once

#include <vultra/assets/texture_data.hpp>
#ifdef VULTRA_WITH_TEXTURE_IMPORT
#include <vultra/assets/source_file.hpp>
#endif

namespace vultra
{
    enum class TextureDimension
    {
        e2D,
        e2DArray,
        e3D,
        eCube,
        eCubeArray
    };

    struct TextureSubresource
    {
        Extent                 size;
        uint32_t               depth = 1;
        uint32_t               mip   = 0;
        uint32_t               layer = 0;
        std::vector<std::byte> bytes;
    };

    // Authored DDS subresources retain their mip chain and compression. Other images use the 2D importer.
    struct TextureAssetData
    {
        TextureDimension                dimension = TextureDimension::e2D;
        TextureFormat                   format    = TextureFormat::eUnknown;
        uint32_t                        layers    = 1;
        uint32_t                        mips      = 1;
        std::vector<TextureSubresource> subresources;
    };

#ifdef VULTRA_WITH_TEXTURE_IMPORT
    TextureAssetData loadTextureAsset(const std::filesystem::path& path,
                                      bool                         srgb,
                                      const SourceObserver&        observer = {},
                                      const AssetSource*           source   = nullptr);
#endif
} // namespace vultra
