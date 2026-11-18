#pragma once

#include <vultra/assets/imported_asset.hpp>
#include <vultra/assets/texture_import.hpp>
#include <vultra/core/base/error.hpp>

#include <expected>
#include <functional>
#include <string>
#include <vector>

namespace vultra
{
    // Derives buffers from a model when it is imported. Its output is cached with the model: the cache key includes
    // its name and version, so a cache hit, as in a packed project, does not run it. Raise the version when the
    // output changes.
    struct GeometryProcessor
    {
        std::string name;
        uint32_t    version = 1;
        std::function<std::expected<std::vector<GeometryBuffer>, Error>(const SceneData& model, uint32_t workers)>
            process;
    };

    struct AssetImportOptions
    {
        std::filesystem::path cacheDirectory = ".vultra/assets";
        TextureImportOptions  textures;
        FbxImportOptions      fbx;
        uint32_t              workers  = 0; // Maximum import concurrency; zero selects automatically.
        bool                  cache    = true;
        bool                  reimport = false;
        // Run in this order on the model; their buffers' names must differ.
        std::vector<GeometryProcessor> processors;
    };

    // Source files stay untouched. Changed dependencies/options invalidate the derived cache.
    // A rejected cache is logged and rebuilt; importer errors remain errors.
    // A cache made through an asset source names files relative to the source's root: it remains current when
    // the root directory and the cache directory are moved together. Without one, it names absolute paths.
    [[nodiscard]] std::expected<ImportedAsset, Error> importAsset(const std::filesystem::path& source,
                                                                  const AssetImportOptions&    options     = {},
                                                                  const AssetSource*           assetSource = nullptr);

    // Check archive integrity, recipe and source content without parsing geometry or decoding images.
    // Used by build-time preparation; a missing/stale/corrupt cache returns false. An error means the source path
    // itself could not be resolved.
    [[nodiscard]] std::expected<bool, Error> isAssetCacheCurrent(const std::filesystem::path& source,
                                                                 const AssetImportOptions&    options     = {},
                                                                 const AssetSource*           assetSource = nullptr);
} // namespace vultra
