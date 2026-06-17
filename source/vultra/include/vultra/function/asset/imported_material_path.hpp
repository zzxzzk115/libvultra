#pragma once

// Imported-material asset path helpers.
//
// Shared between the asset registry/import scan (asset_system.cpp::configure) and the
// material upload/emit path (asset_material_upload.cpp::emitImportedMaterialAssets), which
// live in different translation units. Kept header-inline so both units derive the imported
// material's relative path from a single source of truth.

#include <cctype>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace vultra::asset_detail
{
    inline std::string sanitizeMaterialAssetSegment(std::string text)
    {
        if (text.empty())
            text = "material";
        for (char& c : text)
        {
            const auto ch = static_cast<unsigned char>(c);
            if (!std::isalnum(ch) && c != '_' && c != '-' && c != '.')
                c = '_';
        }
        while (!text.empty() && (text.front() == '_' || text.front() == '.'))
            text.erase(text.begin());
        if (text.empty())
            text = "material";
        return text;
    }

    inline std::filesystem::path importedMaterialAssetRelativePath(const std::string_view meshImportedPath,
                                                                   const uint32_t         slot,
                                                                   const std::string_view materialName)
    {
        const auto meshKey =
            sanitizeMaterialAssetSegment(std::filesystem::path(std::string(meshImportedPath)).filename().generic_string());
        const auto materialKey = sanitizeMaterialAssetSegment(std::string(materialName));
        return std::filesystem::path("materials") / "imported" / meshKey /
               (std::to_string(slot) + "_" + materialKey + ".vmat.json");
    }
} // namespace vultra::asset_detail
