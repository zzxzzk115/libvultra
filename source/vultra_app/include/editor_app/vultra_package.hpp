#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace vasset
{
    class VAssetRegistry;
}

namespace vultra_app
{
    struct VultraPackageExportResult
    {
        bool        ok {false};
        std::string error;
        size_t      filesWritten {0};
    };

    struct VultraPackageImportResult
    {
        bool        ok {false};
        std::string error;
        size_t      filesWritten {0};
        size_t      filesSkipped {0};
        std::vector<std::filesystem::path> sourcePaths;
    };

    struct VultraPackageValidationResult
    {
        bool                     ok {false};
        std::string              error;
        size_t                   filesChecked {0};
        std::vector<std::string> missingDependencies;
    };

    std::filesystem::path ensureVultraPackageExtension(std::filesystem::path path);

    VultraPackageValidationResult validateVultraPackage(const std::filesystem::path& packagePath);

    VultraPackageExportResult exportVultraPackage(const std::filesystem::path&              assetRoot,
                                                  const std::filesystem::path&              packagePath,
                                                  const std::vector<std::filesystem::path>& sourcePaths,
                                                  const vasset::VAssetRegistry*             registry = nullptr);

    VultraPackageImportResult importVultraPackage(const std::filesystem::path& assetRoot,
                                                  const std::filesystem::path& packagePath);
} // namespace vultra_app
