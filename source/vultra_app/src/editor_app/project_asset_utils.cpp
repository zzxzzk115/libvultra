#include "editor_app/project_asset_utils.hpp"

#include <algorithm>
#include <system_error>

namespace vultra_app
{
    bool isImportedAssetRelativePath(const std::filesystem::path& relativePath)
    {
        return !relativePath.empty() && *relativePath.begin() == "imported";
    }

    std::filesystem::path projectAssetRootPath(const std::filesystem::path& projectRoot, std::string_view assetRootName)
    {
        if (projectRoot.empty() || assetRootName.empty())
            return {};
        return (projectRoot / std::filesystem::path(std::string(assetRootName))).lexically_normal();
    }

    std::string projectAssetUriForRelativePath(const std::filesystem::path& relativePath)
    {
        if (relativePath.empty() || isImportedAssetRelativePath(relativePath))
            return {};
        return "res://" + relativePath.generic_string();
    }

    std::vector<std::string> collectProjectAssetUrisWithExtension(const std::filesystem::path& projectRoot,
                                                                  std::string_view              assetRootName,
                                                                  std::string_view              extension)
    {
        std::vector<std::string> uris;
        const auto               assetRoot = projectAssetRootPath(projectRoot, assetRootName);
        if (assetRoot.empty())
            return uris;

        std::error_code ec;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(assetRoot, ec))
        {
            if (ec)
                break;
            if (!entry.is_regular_file(ec) || entry.path().extension().generic_string() != extension)
                continue;

            const auto rel = std::filesystem::relative(entry.path().lexically_normal(), assetRoot, ec);
            if (ec || isImportedAssetRelativePath(rel))
                continue;

            uris.push_back(projectAssetUriForRelativePath(rel));
        }

        std::sort(uris.begin(), uris.end());
        uris.erase(std::unique(uris.begin(), uris.end()), uris.end());
        return uris;
    }

    std::vector<std::string> collectProjectAssetUrisWithSuffix(const std::filesystem::path& projectRoot,
                                                               std::string_view              assetRootName,
                                                               std::string_view              suffix)
    {
        std::vector<std::string> uris;
        const auto               assetRoot = projectAssetRootPath(projectRoot, assetRootName);
        if (assetRoot.empty())
            return uris;

        std::error_code ec;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(assetRoot, ec))
        {
            if (ec)
                break;
            if (!entry.is_regular_file(ec))
                continue;

            const auto rel = std::filesystem::relative(entry.path().lexically_normal(), assetRoot, ec);
            if (ec || isImportedAssetRelativePath(rel))
                continue;

            const auto name = entry.path().filename().generic_string();
            if (name.ends_with(suffix))
                uris.push_back(projectAssetUriForRelativePath(rel));
        }

        std::sort(uris.begin(), uris.end());
        uris.erase(std::unique(uris.begin(), uris.end()), uris.end());
        return uris;
    }

    std::vector<std::filesystem::path> collectProjectAssetFilesWithSuffix(const std::filesystem::path& projectRoot,
                                                                          std::string_view              assetRootName,
                                                                          std::string_view              suffix)
    {
        std::vector<std::filesystem::path> files;
        const auto                         assetRoot = projectAssetRootPath(projectRoot, assetRootName);
        if (assetRoot.empty())
            return files;

        std::error_code ec;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(assetRoot, ec))
        {
            if (ec)
                break;
            if (!entry.is_regular_file(ec))
                continue;

            const auto rel = std::filesystem::relative(entry.path().lexically_normal(), assetRoot, ec);
            if (ec || isImportedAssetRelativePath(rel))
                continue;

            const auto name = entry.path().filename().generic_string();
            if (name.ends_with(suffix))
                files.push_back(entry.path().lexically_normal());
        }

        std::sort(files.begin(), files.end());
        files.erase(std::unique(files.begin(), files.end()), files.end());
        return files;
    }
}
