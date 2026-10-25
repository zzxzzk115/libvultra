#pragma once

#include <vultra/assets/scene_render_data.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace vultra
{
    struct AssetDependency
    {
        std::filesystem::path path;
        std::string           hash; // XXH3-64 of the exact bytes consumed by the importer.
    };

    // Prepared CPU data can be owned and consumed without linking any importer.
    struct ImportedAsset
    {
        SceneData                    scene;
        PreparedTextures             textures;
        bool                         cacheHit = false;
        std::filesystem::path        cachePath;
        std::vector<AssetDependency> dependencies;
    };
} // namespace vultra
