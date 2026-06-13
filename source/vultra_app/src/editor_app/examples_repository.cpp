#include "editor_app/examples_repository.hpp"

#include "common/remote_fetch.hpp"

#include <chrono>
#include <fstream>
#include <nlohmann/json.hpp>
#include <optional>
#include <system_error>

namespace vultra_app::examples
{
    namespace
    {
        namespace fs = std::filesystem;

        fs::path examplesRoot(const fs::path& cacheRoot)
        {
            return (cacheRoot / ".vultra" / "examples").lexically_normal();
        }

        std::optional<fs::path> findProjectFile(const fs::path& root)
        {
            std::error_code ec;
            if (root.empty() || !fs::exists(root, ec))
                return std::nullopt;

            // Prefer a .vproject sitting at the repository root.
            for (const auto& entry : fs::directory_iterator(root, ec))
            {
                if (ec)
                    break;
                if (entry.is_regular_file(ec) && entry.path().extension() == ".vproject")
                    return entry.path();
            }
            for (const auto& entry : fs::recursive_directory_iterator(root, ec))
            {
                if (ec)
                    break;
                if (entry.is_regular_file(ec) && entry.path().extension() == ".vproject")
                    return entry.path();
            }
            return std::nullopt;
        }

        // Lets git::syncCache reuse a pre-existing non-git payload that already holds a project.
        bool cacheHoldsProject(const fs::path& dir) { return findProjectFile(dir).has_value(); }
    } // namespace

    std::string defaultExamplesCatalogUrl()
    {
        return "https://raw.githubusercontent.com/zzxzzk115/vultra-examples/main/examples.json";
    }

    bool fetchExamplesCatalog(const fs::path&            cacheRoot,
                              const std::string&         location,
                              std::vector<ExampleEntry>& entries,
                              std::string&               status)
    {
        entries.clear();
        if (location.empty())
        {
            status = "Examples catalog location is empty.";
            return false;
        }

        std::error_code ec;
        fs::path        catalogPath {location};
        std::string     offlineNote;
        if (net::isHttpUrl(location))
        {
            catalogPath = (examplesRoot(cacheRoot) / "catalogs" / (net::readableRepoName(location) + ".json"))
                              .lexically_normal();
            // Defeat the CDN edge cache on a manual refresh with a throwaway query parameter.
            std::string downloadUrl = location;
            downloadUrl += downloadUrl.find('?') == std::string::npos ? '?' : '&';
            downloadUrl += "nocache=" + std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
                                                           std::chrono::system_clock::now().time_since_epoch())
                                                           .count());
            std::string downloadStatus;
            if (!net::downloadToFile(downloadUrl, catalogPath, downloadStatus))
            {
                if (!fs::exists(catalogPath, ec))
                {
                    status = downloadStatus;
                    return false;
                }
                offlineNote = " (offline: showing cached catalog; " + downloadStatus + ")";
            }
        }

        std::ifstream file(catalogPath);
        if (!file)
        {
            status = "Examples catalog could not be opened.";
            return false;
        }

        auto json = nlohmann::json::parse(file, nullptr, false);
        if (json.is_discarded() || !json.is_object() ||
            !json.value("examples", nlohmann::json::array()).is_array())
        {
            status = "Examples catalog is not valid JSON.";
            return false;
        }

        for (const auto& item : json.value("examples", nlohmann::json::array()))
        {
            if (!item.is_object())
                continue;
            ExampleEntry entry;
            entry.id           = item.value("id", std::string {});
            entry.name         = item.value("name", std::string {});
            entry.author       = item.value("author", std::string {});
            entry.description  = item.value("description", std::string {});
            entry.repository   = item.value("repository", std::string {});
            entry.thumbnailUrl = item.value("thumbnailUrl", std::string {});
            entry.version      = item.value("version", std::string {});
            if (const auto it = item.find("tags"); it != item.end() && it->is_array())
            {
                for (const auto& tag : *it)
                    if (tag.is_string())
                        entry.tags.push_back(tag.get<std::string>());
            }

            // Source: a nested {type:"git", url, ref} object, or flat gitUrl/gitRef fields.
            const auto source = item.value("source", nlohmann::json::object());
            if (source.is_object() && source.value("type", std::string {}) == "git")
            {
                entry.gitUrl = source.value("url", std::string {});
                entry.gitRef = source.value("ref", std::string {});
            }
            if (entry.gitUrl.empty())
                entry.gitUrl = item.value("gitUrl", std::string {});
            if (entry.gitRef.empty())
                entry.gitRef = item.value("gitRef", std::string {});

            if (entry.id.empty() || entry.gitUrl.empty())
                continue;
            entries.push_back(std::move(entry));
        }

        if (entries.empty())
        {
            status = "Catalog did not contain any git-backed examples.";
            return false;
        }

        status = "Loaded " + std::to_string(entries.size()) + " example(s)." + offlineNote;
        return true;
    }

    fs::path cachedThumbnail(const fs::path& cacheRoot, const ExampleEntry& entry, std::string& status)
    {
        if (entry.thumbnailUrl.empty())
        {
            status = "Example has no thumbnail.";
            return {};
        }

        const auto slug = net::safeCacheName(entry.id.empty() ? entry.thumbnailUrl : entry.id);
        const auto dest = (examplesRoot(cacheRoot) / "thumbnails" / (slug + ".png")).lexically_normal();
        return net::cachedImage(dest, entry.thumbnailUrl, status);
    }

    ForkResult forkExample(const fs::path& cacheRoot, const ExampleEntry& entry, const fs::path& destDir)
    {
        ForkResult result;
        if (entry.gitUrl.empty())
        {
            result.status = "Example has no git source.";
            return result;
        }

        const auto      dest = destDir.lexically_normal();
        std::error_code ec;
        if (dest.empty())
        {
            result.status = "Destination folder is empty.";
            return result;
        }
        if (fs::exists(dest, ec) && !fs::is_empty(dest, ec))
        {
            result.status = "Destination folder is not empty.";
            return result;
        }

        const auto cacheDir =
            (examplesRoot(cacheRoot) / ".cache" / net::readableRepoName(entry.gitUrl)).lexically_normal();
        fs::create_directories(cacheDir.parent_path(), ec);
        if (ec)
        {
            result.status = "Failed to create example cache: " + ec.message();
            return result;
        }

        std::string cacheWarning;
        if (!git::syncCache(cacheDir, entry.gitUrl, entry.gitRef, cacheHoldsProject, cacheWarning, result.status))
        {
            result.status = "Fork failed: " + result.status;
            return result;
        }

        const auto projectFile = findProjectFile(cacheDir);
        if (!projectFile.has_value())
        {
            result.status = "Cloned repository does not contain a .vproject.";
            return result;
        }

        std::string copyError;
        if (!git::copyPayloadStripGit(projectFile->parent_path(), dest, copyError))
        {
            result.status = "Fork failed: " + copyError;
            return result;
        }

        const auto destProject = findProjectFile(dest);
        if (!destProject.has_value())
        {
            result.status = "Forked project is missing its .vproject.";
            return result;
        }

        result.ok          = true;
        result.projectFile = *destProject;
        result.status      = "Forked '" + (entry.name.empty() ? entry.id : entry.name) + "' to " +
                        dest.generic_string() + "." + cacheWarning;
        return result;
    }
} // namespace vultra_app::examples
