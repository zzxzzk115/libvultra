#include "editor_app/templates_repository.hpp"

#include "common/remote_fetch.hpp"
#include "editor_app/plugin_repository.hpp" // plugins::compareVersions

#include <chrono>
#include <fstream>
#include <nlohmann/json.hpp>
#include <optional>
#include <system_error>

namespace vultra_app::templates
{
    namespace
    {
        namespace fs = std::filesystem;

        fs::path templatesRoot(const fs::path& cacheRoot)
        {
            return (cacheRoot / ".vultra" / "templates").lexically_normal();
        }

        // Choose the template content folder for the running engine version: an exact match, else the
        // highest version folder <= engineVersion, else a "default" folder.
        std::optional<fs::path> pickVersionDir(const fs::path& repoRoot, const std::string& engineVersion)
        {
            std::error_code ec;
            if (!fs::exists(repoRoot, ec))
                return std::nullopt;

            const auto exact = repoRoot / engineVersion;
            if (fs::exists(exact, ec) && fs::is_directory(exact, ec))
                return exact;

            fs::path    best;
            std::string bestName;
            for (const auto& entry : fs::directory_iterator(repoRoot, ec))
            {
                if (ec || !entry.is_directory(ec))
                    continue;
                const auto name = entry.path().filename().generic_string();
                if (name == ".git" || name == "default")
                    continue;
                // Only consider versions the running engine satisfies.
                if (plugins::compareVersions(name, engineVersion) > 0)
                    continue;
                if (best.empty() || plugins::compareVersions(name, bestName) > 0)
                {
                    best     = entry.path();
                    bestName = name;
                }
            }
            if (!best.empty())
                return best;

            const auto fallback = repoRoot / "default";
            if (fs::exists(fallback, ec) && fs::is_directory(fallback, ec))
                return fallback;
            return std::nullopt;
        }

        bool cacheHoldsTemplate(const fs::path& dir)
        {
            std::error_code ec;
            // A reusable non-git cache must contain at least one version-like folder.
            for (const auto& entry : fs::directory_iterator(dir, ec))
            {
                if (ec)
                    break;
                if (entry.is_directory(ec) && entry.path().filename() != ".git")
                    return true;
            }
            return false;
        }
    } // namespace

    std::string defaultTemplatesCatalogUrl()
    {
        return "https://raw.githubusercontent.com/zzxzzk115/vultra-templates/main/templates.json";
    }

    bool fetchTemplatesCatalog(const fs::path&             cacheRoot,
                               const std::string&          location,
                               std::vector<TemplateEntry>& entries,
                               std::string&                status)
    {
        entries.clear();
        if (location.empty())
        {
            status = "Templates catalog location is empty.";
            return false;
        }

        std::error_code ec;
        fs::path        catalogPath {location};
        std::string     offlineNote;
        if (net::isHttpUrl(location))
        {
            catalogPath = (templatesRoot(cacheRoot) / "catalogs" / (net::readableRepoName(location) + ".json"))
                              .lexically_normal();
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
            status = "Templates catalog could not be opened.";
            return false;
        }

        auto json = nlohmann::json::parse(file, nullptr, false);
        if (json.is_discarded() || !json.is_object() ||
            !json.value("templates", nlohmann::json::array()).is_array())
        {
            status = "Templates catalog is not valid JSON.";
            return false;
        }

        for (const auto& item : json.value("templates", nlohmann::json::array()))
        {
            if (!item.is_object())
                continue;
            TemplateEntry entry;
            entry.id                 = item.value("id", std::string {});
            entry.name               = item.value("name", std::string {});
            entry.description        = item.value("description", std::string {});
            entry.thumbnailUrl       = item.value("thumbnailUrl", std::string {});
            entry.gitUrl             = item.value("gitUrl", std::string {});
            entry.defaultRenderGraph = item.value("defaultRenderGraph", std::string {});
            if (entry.id.empty() || entry.gitUrl.empty())
                continue;
            entries.push_back(std::move(entry));
        }

        if (entries.empty())
        {
            status = "Catalog did not contain any templates.";
            return false;
        }

        status = "Loaded " + std::to_string(entries.size()) + " template(s)." + offlineNote;
        return true;
    }

    fs::path cachedThumbnail(const fs::path& cacheRoot, const TemplateEntry& entry, std::string& status)
    {
        if (entry.thumbnailUrl.empty())
        {
            status = "Template has no thumbnail.";
            return {};
        }
        const auto slug = net::safeCacheName(entry.id.empty() ? entry.thumbnailUrl : entry.id);
        const auto dest = (templatesRoot(cacheRoot) / "thumbnails" / (slug + ".png")).lexically_normal();
        return net::cachedImage(dest, entry.thumbnailUrl, status);
    }

    bool materializeTemplate(const fs::path&      cacheRoot,
                             const TemplateEntry& entry,
                             const std::string&   engineVersion,
                             const fs::path&      destDir,
                             std::string&         status)
    {
        if (entry.gitUrl.empty())
        {
            status = "Template has no git source.";
            return false;
        }

        const auto cacheDir =
            (templatesRoot(cacheRoot) / ".cache" / net::readableRepoName(entry.gitUrl)).lexically_normal();
        std::error_code ec;
        fs::create_directories(cacheDir.parent_path(), ec);

        std::string cacheWarning;
        if (!git::syncCache(cacheDir, entry.gitUrl, std::string {}, cacheHoldsTemplate, cacheWarning, status))
        {
            status = "Template fetch failed: " + status;
            return false;
        }

        const auto versionDir = pickVersionDir(cacheDir, engineVersion);
        if (!versionDir.has_value())
        {
            status = "Template '" + (entry.name.empty() ? entry.id : entry.name) +
                     "' has no content for engine " + engineVersion + ".";
            return false;
        }

        std::string copyError;
        if (!git::copyPayloadStripGit(*versionDir, destDir, copyError))
        {
            status = "Template copy failed: " + copyError;
            return false;
        }
        return true;
    }
} // namespace vultra_app::templates
