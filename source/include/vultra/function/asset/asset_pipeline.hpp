#pragma once

#include <vultra/function/asset/texture_import.hpp>

namespace vultra
{
    struct AssetImportOptions
    {
        std::filesystem::path cacheDirectory = ".vultra/assets";
        TextureImportOptions  textures;
        uint32_t              workers  = 0; // Maximum import concurrency; zero selects automatically.
        bool                  cache    = true;
        bool                  reimport = false;
    };

    struct ImportedAsset
    {
        Scene                 scene;
        PreparedTextures      textures;
        bool                  cacheHit = false;
        std::filesystem::path cachePath;
    };

    // Source files stay untouched. Changed dependencies/options invalidate the derived cache.
    // A rejected cache is logged and rebuilt; importer errors remain errors.
    ImportedAsset importAsset(const std::filesystem::path& source, const AssetImportOptions& options = {});

    // Check archive integrity, recipe and source content without parsing geometry or decoding images.
    // Used by build-time preparation; a missing/stale/corrupt cache returns false.
    bool isAssetCacheCurrent(const std::filesystem::path& source, const AssetImportOptions& options = {});
} // namespace vultra
