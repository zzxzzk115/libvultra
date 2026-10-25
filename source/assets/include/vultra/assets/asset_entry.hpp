#pragma once

#include <vultra/assets/asset_id.hpp>
#include <vultra/assets/fbx_import.hpp>

#include <filesystem>
#include <optional>

namespace vultra
{
    // Stable asset identity and authoring path; interpretation belongs to the consuming application.
    struct AssetEntry
    {
        AssetId                         id;
        std::filesystem::path           path;
        std::optional<FbxImportOptions> fbx;
    };
} // namespace vultra
