#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace vultra_app
{
    bool isImportedAssetRelativePath(const std::filesystem::path& relativePath);

    std::filesystem::path projectAssetRootPath(const std::filesystem::path& projectRoot, std::string_view assetRootName);

    std::string projectAssetUriForRelativePath(const std::filesystem::path& relativePath);

    std::vector<std::string> collectProjectAssetUrisWithExtension(const std::filesystem::path& projectRoot,
                                                                  std::string_view              assetRootName,
                                                                  std::string_view              extension);

    std::vector<std::string> collectProjectAssetUrisWithSuffix(const std::filesystem::path& projectRoot,
                                                               std::string_view              assetRootName,
                                                               std::string_view              suffix);

    std::vector<std::filesystem::path> collectProjectAssetFilesWithSuffix(const std::filesystem::path& projectRoot,
                                                                          std::string_view              assetRootName,
                                                                          std::string_view              suffix);
}
