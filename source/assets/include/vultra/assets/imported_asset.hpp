#pragma once

#include <vultra/assets/scene_render_data.hpp>

#include <cstddef>
#include <cstdint>
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

    // Data a geometry processor derives from one model, such as meshlets: `bytes` holds elements of `elementSize`
    // bytes, and the indices in them count from the model's first vertex, index or primitive.
    struct GeometryBuffer
    {
        std::string            name;
        uint32_t               elementSize = 0;
        std::vector<std::byte> bytes;
    };

    // Prepared CPU data can be owned and consumed without linking any importer.
    struct ImportedAsset
    {
        SceneData                    scene;
        PreparedTextures             textures;
        std::vector<GeometryBuffer>  buffers; // What the import options' geometry processors made, in their order.
        bool                         cacheHit = false;
        std::filesystem::path        cachePath;
        std::vector<AssetDependency> dependencies;
    };
} // namespace vultra
