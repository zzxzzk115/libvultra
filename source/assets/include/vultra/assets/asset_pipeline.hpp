#pragma once

#include <vultra/assets/imported_asset.hpp>
#include <vultra/assets/texture_import.hpp>

namespace vultra
{
    struct AssetImportOptions
    {
        std::filesystem::path cacheDirectory = ".vultra/assets";
        TextureImportOptions  textures;
        FbxImportOptions      fbx;
        uint32_t              workers  = 0; // Maximum import concurrency; zero selects automatically.
        bool                  cache    = true;
        bool                  reimport = false;
    };

    // Source files stay untouched. Changed dependencies/options invalidate the derived cache.
    // A rejected cache is logged and rebuilt; importer errors remain errors.
    // A cache made through an asset source names files relative to the source's root: it remains current when
    // the root directory and the cache directory are moved together. Without one, it names absolute paths.
    ImportedAsset importAsset(const std::filesystem::path& source,
                              const AssetImportOptions&    options     = {},
                              const AssetSource*           assetSource = nullptr);

    // Check archive integrity, recipe and source content without parsing geometry or decoding images.
    // Used by build-time preparation; a missing/stale/corrupt cache returns false.
    bool isAssetCacheCurrent(const std::filesystem::path& source,
                             const AssetImportOptions&    options     = {},
                             const AssetSource*           assetSource = nullptr);
} // namespace vultra
