#include "editor_app/export_templates_repository.hpp"

#include "common/remote_fetch.hpp"
#include "editor_app/plugin_repository.hpp" // plugins::compareVersions, plugins::engineSupports

#include <chrono>
#include <fstream>
#include <nlohmann/json.hpp>
#include <system_error>

namespace vultra_app::export_templates
{
    namespace
    {
        namespace fs = std::filesystem;

        fs::path exportTemplatesRoot(const fs::path& cacheRoot)
        {
            return (cacheRoot / ".vultra" / "export-templates").lexically_normal();
        }

        fs::path versionDir(const fs::path& cacheRoot, const std::string& platform, const std::string& arch,
                            const std::string& version)
        {
            return (exportTemplatesRoot(cacheRoot) / (platform + "-" + arch) / version).lexically_normal();
        }
    } // namespace

    std::string defaultExportTemplatesCatalogUrl()
    {
        return "https://raw.githubusercontent.com/zzxzzk115/vultra-export-templates/main/export-templates.json";
    }

    std::string catalogPlatform(const std::string& targetPlatform)
    {
        if (targetPlatform == "Windows")
            return "windows";
        if (targetPlatform == "macOS")
            return "macos";
        if (targetPlatform == "Linux")
            return "linux";
        if (targetPlatform == "Android")
            return "android";
        if (targetPlatform == "WebGPU" || targetPlatform == "Web")
            return "wasm";
        // Already lowercase / unknown: pass through.
        return targetPlatform;
    }

    std::string runtimeFileName(const std::string& platform)
    {
        return platform == "windows" ? "vultra-runtime.exe" : "vultra-runtime";
    }

    bool fetchExportTemplatesCatalog(const fs::path&                   cacheRoot,
                                     const std::string&                location,
                                     std::vector<ExportTemplateEntry>& entries,
                                     std::string&                      status)
    {
        entries.clear();
        if (location.empty())
        {
            status = "Export templates catalog location is empty.";
            return false;
        }

        std::error_code ec;
        fs::path        catalogPath {location};
        std::string     offlineNote;
        if (net::isHttpUrl(location))
        {
            catalogPath =
                (exportTemplatesRoot(cacheRoot) / "catalogs" / (net::readableRepoName(location) + ".json"))
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
            status = "Export templates catalog could not be opened.";
            return false;
        }

        auto json = nlohmann::json::parse(file, nullptr, false);
        if (json.is_discarded() || !json.is_object() ||
            !json.value("templates", nlohmann::json::array()).is_array())
        {
            status = "Export templates catalog is not valid JSON.";
            return false;
        }

        for (const auto& item : json.value("templates", nlohmann::json::array()))
        {
            if (!item.is_object())
                continue;
            ExportTemplateEntry entry;
            entry.id       = item.value("id", std::string {});
            entry.platform = item.value("platform", std::string {});
            entry.arch     = item.value("arch", std::string {});
            entry.name     = item.value("name", std::string {});
            for (const auto& v : item.value("versions", nlohmann::json::array()))
            {
                if (!v.is_object())
                    continue;
                ExportTemplateVersion version;
                version.version          = v.value("version", std::string {});
                version.minEngineVersion = v.value("minEngineVersion", std::string {});
                version.url              = v.value("url", std::string {});
                version.sha256           = v.value("sha256", std::string {});
                version.size             = v.value("size", std::uint64_t {0});
                if (version.version.empty() || version.url.empty())
                    continue;
                entry.versions.push_back(std::move(version));
            }
            if (entry.platform.empty() || entry.arch.empty() || entry.versions.empty())
                continue;
            if (entry.id.empty())
                entry.id = entry.platform + "-" + entry.arch;
            entries.push_back(std::move(entry));
        }

        if (entries.empty())
        {
            status = "Catalog did not contain any export templates.";
            return false;
        }

        status = "Loaded " + std::to_string(entries.size()) + " export template(s)." + offlineNote;
        return true;
    }

    const ExportTemplateEntry* findEntry(const std::vector<ExportTemplateEntry>& entries,
                                         const std::string&                      platform,
                                         const std::string&                      arch)
    {
        for (const auto& entry : entries)
            if (entry.platform == platform && entry.arch == arch)
                return &entry;
        return nullptr;
    }

    const ExportTemplateVersion* bestVersion(const ExportTemplateEntry& entry, const std::string& engineVersion)
    {
        const ExportTemplateVersion* best = nullptr;
        for (const auto& version : entry.versions)
        {
            if (version.version == engineVersion)
                return &version; // exact match wins outright
            if (!plugins::engineSupports(version.minEngineVersion))
                continue;
            if (best == nullptr || plugins::compareVersions(version.version, best->version) > 0)
                best = &version;
        }
        return best;
    }

    fs::path materializeExportTemplate(const fs::path&              cacheRoot,
                                       const ExportTemplateEntry&   entry,
                                       const ExportTemplateVersion& version,
                                       std::string&                 status)
    {
        if (version.url.empty())
        {
            status = "Export template has no download URL.";
            return {};
        }

        const auto dir  = versionDir(cacheRoot, entry.platform, entry.arch, version.version);
        const auto dest = (dir / runtimeFileName(entry.platform)).lexically_normal();

        std::error_code ec;
        if (fs::exists(dest, ec) && fs::is_regular_file(dest, ec))
        {
            // Reuse the cached binary unless an expected size is given and disagrees.
            if (version.size == 0 || fs::file_size(dest, ec) == version.size)
            {
                status = "Using cached export template " + version.version + ".";
                return dest;
            }
            fs::remove(dest, ec);
        }

        std::string downloadStatus;
        if (!net::downloadToFile(version.url, dest, downloadStatus))
        {
            status = "Export template download failed: " + downloadStatus;
            return {};
        }

        if (version.size != 0)
        {
            const auto actual = fs::file_size(dest, ec);
            if (ec || actual != version.size)
            {
                fs::remove(dest, ec);
                status = "Export template size mismatch (expected " + std::to_string(version.size) +
                         ", got " + std::to_string(actual) + ").";
                return {};
            }
        }
        // sha256 is recorded in the catalog for provenance; verification is left to a future crypto
        // helper (the engine ships no hash utility yet) and size is the integrity gate for now.

        status = "Downloaded export template " + version.version + ".";
        return dest;
    }

    fs::path cachedExportTemplate(const fs::path&    cacheRoot,
                                  const std::string& platform,
                                  const std::string& arch,
                                  const std::string& engineVersion)
    {
        const auto root = (exportTemplatesRoot(cacheRoot) / (platform + "-" + arch)).lexically_normal();
        std::error_code ec;
        if (!fs::exists(root, ec))
            return {};

        const auto runtimeName = runtimeFileName(platform);
        fs::path    best;
        std::string bestVersionName;
        for (const auto& dirEntry : fs::directory_iterator(root, ec))
        {
            if (ec || !dirEntry.is_directory(ec))
                continue;
            const auto candidate = dirEntry.path() / runtimeName;
            if (!fs::exists(candidate, ec) || !fs::is_regular_file(candidate, ec))
                continue;
            const auto name = dirEntry.path().filename().generic_string();
            // Exact engine-version match is preferred immediately.
            if (name == engineVersion)
                return candidate.lexically_normal();
            if (best.empty() || plugins::compareVersions(name, bestVersionName) > 0)
            {
                best            = candidate;
                bestVersionName = name;
            }
        }
        return best.empty() ? fs::path {} : best.lexically_normal();
    }
} // namespace vultra_app::export_templates
