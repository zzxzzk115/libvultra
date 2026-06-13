#include "editor_app/plugin_repository.hpp"

#include "common/remote_fetch.hpp"

#include <vultra/core/base/common_context.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

namespace vultra_app::plugins
{
    namespace
    {
        namespace fs = std::filesystem;

        constexpr const char* kLockFileName = "vultra.plugins.lock";

        fs::path gitCacheRoot(const fs::path& projectRoot) { return managedRoot(projectRoot) / ".cache"; }

        fs::path absoluteLockDir(const fs::path& projectRoot, const std::string& directory)
        {
            fs::path path {directory};
            if (!path.is_absolute())
                path = projectRoot / path;
            return path.lexically_normal();
        }

        std::optional<fs::path> findPluginRoot(const fs::path& root)
        {
            std::error_code ec;
            if (root.empty() || !fs::exists(root, ec))
                return std::nullopt;

            const auto direct = root / vultra::kPluginManifestFile;
            if (fs::exists(direct, ec))
                return root;

            for (const auto& entry : fs::recursive_directory_iterator(root, ec))
            {
                if (ec)
                    break;
                if (!entry.is_regular_file(ec))
                    continue;
                if (entry.path().filename() == vultra::kPluginManifestFile)
                    return entry.path().parent_path();
            }
            return std::nullopt;
        }

        // True when a cached folder already holds a usable plugin payload (no .git needed).
        bool cacheHoldsPlugin(const fs::path& cacheDir) { return findPluginRoot(cacheDir).has_value(); }

        std::uintmax_t directoryFileCount(const fs::path& dir)
        {
            std::error_code ec;
            std::uintmax_t  count = 0;
            for (const auto& entry : fs::recursive_directory_iterator(dir, ec))
            {
                if (ec)
                    break;
                if (entry.is_regular_file(ec))
                    ++count;
            }
            return count;
        }

        std::uintmax_t directoryByteSize(const fs::path& dir)
        {
            std::error_code ec;
            std::uintmax_t  bytes = 0;
            for (const auto& entry : fs::recursive_directory_iterator(dir, ec))
            {
                if (ec)
                    break;
                if (entry.is_regular_file(ec))
                    bytes += entry.file_size(ec);
            }
            return bytes;
        }

        std::string textFileHash(const fs::path& path)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file)
                return {};
            std::ostringstream buffer;
            buffer << file.rdbuf();
            std::ostringstream out;
            out << std::hex << std::hash<std::string> {}(buffer.str());
            return out.str();
        }

        nlohmann::json loadLock(const fs::path& lockPath)
        {
            std::ifstream file(lockPath);
            if (!file)
                return nlohmann::json {{"schemaVersion", 1}, {"plugins", nlohmann::json::array()}};
            auto json = nlohmann::json::parse(file, nullptr, false);
            if (json.is_discarded() || !json.is_object())
                return nlohmann::json {{"schemaVersion", 1}, {"plugins", nlohmann::json::array()}};
            if (!json.contains("plugins") || !json["plugins"].is_array())
                json["plugins"] = nlohmann::json::array();
            json["schemaVersion"] = json.value("schemaVersion", 1);
            return json;
        }

        nlohmann::json lockEntryFor(const fs::path&               projectRoot,
                                    const vultra::PluginManifest& manifest,
                                    nlohmann::json                base)
        {
            std::error_code ec;
            base["id"]           = manifest.id;
            base["name"]         = manifest.name;
            base["version"]      = manifest.version;
            base["directory"]    = fs::relative(manifest.directory, projectRoot, ec).generic_string();
            base["manifestHash"] = textFileHash(manifest.manifestPath);
            base["fileCount"]    = directoryFileCount(manifest.directory);
            base["byteSize"]     = directoryByteSize(manifest.directory);
            if (!base.contains("source"))
                base["source"] = nlohmann::json {{"type", "unknown"}};
            return base;
        }

        // Rebuild vultra.plugins.lock: a fresh import is authoritative for its id; managed (git)
        // entries are kept (refreshed from disk when their version directory exists -- a missing
        // one stays restorable from its source); local entries are rescanned from the install dir.
        void saveLock(const fs::path&                              projectRoot,
                      const fs::path&                              localPluginsDir,
                      const std::optional<vultra::PluginManifest>& importedManifest,
                      const nlohmann::json&                        importedSource,
                      const std::vector<std::string>&              excludedIds = {})
        {
            std::error_code ec;
            fs::create_directories(projectRoot, ec);
            const auto lockPath = projectRoot / kLockFileName;

            const auto excluded = [&](const std::string& id) {
                return std::find(excludedIds.begin(), excludedIds.end(), id) != excludedIds.end();
            };

            nlohmann::json                  pluginsJson = nlohmann::json::array();
            std::unordered_set<std::string> written;

            if (importedManifest.has_value() && !excluded(importedManifest->id))
            {
                nlohmann::json entry = nlohmann::json::object();
                entry["source"]      = importedSource;
                pluginsJson.push_back(lockEntryFor(projectRoot, *importedManifest, std::move(entry)));
                written.insert(importedManifest->id);
            }

            const auto oldLock = loadLock(lockPath);
            for (const auto& item : oldLock.value("plugins", nlohmann::json::array()))
            {
                const auto id = item.value("id", std::string {});
                if (id.empty() || excluded(id) || written.contains(id))
                    continue;
                const auto source = item.value("source", nlohmann::json::object());
                if (source.value("type", std::string {}) != "git")
                    continue; // local entries are rescanned below

                const auto dir = absoluteLockDir(projectRoot, item.value("directory", std::string {}));
                if (auto manifest = vultra::loadPluginManifest(dir / vultra::kPluginManifestFile); manifest.has_value())
                    pluginsJson.push_back(lockEntryFor(projectRoot, *manifest, item));
                else
                    pluginsJson.push_back(item); // restorable from source; keep as recorded
                written.insert(id);
            }

            for (const auto& manifest : discoverProjectPlugins({localPluginsDir}))
            {
                if (excluded(manifest.id) || written.contains(manifest.id))
                    continue;
                nlohmann::json base = nlohmann::json::object();
                for (const auto& item : oldLock.value("plugins", nlohmann::json::array()))
                {
                    if (item.value("id", std::string {}) == manifest.id)
                    {
                        base = item;
                        break;
                    }
                }
                pluginsJson.push_back(lockEntryFor(projectRoot, manifest, std::move(base)));
                written.insert(manifest.id);
            }

            const nlohmann::json lock = {{"schemaVersion", 1}, {"plugins", std::move(pluginsJson)}};
            std::ofstream        out(lockPath);
            if (out)
                out << lock.dump(2) << "\n";
        }

        bool copyPluginDirectory(const fs::path& source, const fs::path& destination, std::string& error)
        {
            std::error_code ec;
            if (fs::equivalent(source, destination, ec))
                return true;

            fs::remove_all(destination, ec);
            if (ec)
            {
                error = "failed to replace existing plugin folder: " + ec.message();
                return false;
            }

            fs::create_directories(destination.parent_path(), ec);
            if (ec)
            {
                error = "failed to create plugin folder: " + ec.message();
                return false;
            }

            fs::copy(source, destination, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
            if (ec)
            {
                error = "failed to copy plugin files: " + ec.message();
                return false;
            }
            return true;
        }

        ImportResult installLocalPluginFromRoot(const fs::path&       projectRoot,
                                                const fs::path&       pluginsDir,
                                                const fs::path&       sourceRoot,
                                                const nlohmann::json& sourceInfo)
        {
            ImportResult result;
            std::string  manifestError;
            auto manifest = vultra::loadPluginManifest(sourceRoot / vultra::kPluginManifestFile, &manifestError);
            if (!manifest.has_value())
            {
                result.status = manifestError.empty() ? "not a Vultra plugin folder" : manifestError;
                return result;
            }

            const auto  destination = (pluginsDir / sourceRoot.filename()).lexically_normal();
            std::string copyError;
            if (!copyPluginDirectory(sourceRoot, destination, copyError))
            {
                result.status = copyError;
                return result;
            }

            auto installedManifest =
                vultra::loadPluginManifest(destination / vultra::kPluginManifestFile, &manifestError);
            if (!installedManifest.has_value())
            {
                result.status =
                    manifestError.empty() ? "plugin was copied but its manifest could not be read" : manifestError;
                return result;
            }

            saveLock(projectRoot, pluginsDir, installedManifest, sourceInfo);
            result.ok          = true;
            result.installedId = installedManifest->id;
            result.status = "Installed plugin '" + installedManifest->name + "' (" + installedManifest->id + ").";
            return result;
        }

        std::vector<CatalogVersion> parseCatalogVersions(const nlohmann::json& plugin)
        {
            std::vector<CatalogVersion> versions;

            // Entry-level minimum engine version, inherited by each version unless it overrides it.
            const auto entryMin = plugin.value("minEngineVersion", std::string {});

            const auto parseSource = [](const nlohmann::json& container, CatalogVersion& out) {
                const auto source = container.value("source", nlohmann::json::object());
                if (source.value("type", std::string {}) != "git")
                    return false;
                out.gitUrl = source.value("url", std::string {});
                out.gitRef = source.value("ref", std::string {});
                return !out.gitUrl.empty();
            };

            // Schema v2: versions[] (newest first).
            for (const auto& item : plugin.value("versions", nlohmann::json::array()))
            {
                if (!item.is_object())
                    continue;
                CatalogVersion version;
                version.version          = item.value("version", std::string {});
                version.notes            = item.value("notes", std::string {});
                version.minEngineVersion = item.value("minEngineVersion", entryMin);
                if (parseSource(item, version))
                    versions.push_back(std::move(version));
            }

            // Schema v1 fallback (also covers v2 entries whose versions[] was empty/invalid).
            if (versions.empty())
            {
                CatalogVersion version;
                version.version          = plugin.value("version", std::string {});
                version.minEngineVersion = entryMin;
                if (parseSource(plugin, version))
                    versions.push_back(std::move(version));
            }
            return versions;
        }
    } // namespace

    std::string defaultCatalogUrl()
    {
        return "https://raw.githubusercontent.com/zzxzzk115/vultra-plugins/main/plugins.json";
    }

#ifndef VULTRA_ENGINE_VERSION
#    define VULTRA_ENGINE_VERSION "dev"
#endif

    std::string engineVersion() { return VULTRA_ENGINE_VERSION; }

    bool engineSupports(const std::string& minEngineVersion)
    {
        if (minEngineVersion.empty())
            return true;
        // A non-numeric engine version (e.g. "dev") cannot be ordered; treat it as compatible.
        if (engineVersion() == "dev")
            return true;
        return compareVersions(engineVersion(), minEngineVersion) >= 0;
    }

    std::filesystem::path localInstallDir(const std::filesystem::path& projectRoot, const std::string& assetRoot)
    {
        return (projectRoot / assetRoot / "plugins").lexically_normal();
    }

    std::filesystem::path managedRoot(const std::filesystem::path& projectRoot)
    {
        return (projectRoot / ".vultra" / "plugins").lexically_normal();
    }

    std::vector<std::filesystem::path> discoveryDirs(const std::filesystem::path& projectRoot,
                                                     const std::string&           assetRoot)
    {
        std::vector<std::filesystem::path> dirs {localInstallDir(projectRoot, assetRoot)};
        const auto                         lock = loadLock(projectRoot / kLockFileName);
        for (const auto& item : lock.value("plugins", nlohmann::json::array()))
        {
            const auto directory = item.value("directory", std::string {});
            if (directory.empty())
                continue;
            auto path = absoluteLockDir(projectRoot, directory);
            if (std::find(dirs.begin(), dirs.end(), path) == dirs.end())
                dirs.push_back(std::move(path));
        }
        return dirs;
    }

    std::vector<vultra::PluginManifest> discoverProjectPlugins(const std::vector<std::filesystem::path>& dirs)
    {
        std::vector<vultra::PluginManifest> result;
        std::unordered_set<std::string>     seen;
        for (const auto& dir : dirs)
        {
            for (auto manifest : vultra::discoverPlugins(dir))
            {
                if (manifest.id.empty() || !seen.insert(manifest.id).second)
                    continue;
                result.push_back(std::move(manifest));
            }
        }
        std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
        return result;
    }

    std::vector<std::string> lockedPluginIds(const std::filesystem::path& projectRoot)
    {
        std::vector<std::string> ids;
        const auto               lock = loadLock(projectRoot / kLockFileName);
        for (const auto& item : lock.value("plugins", nlohmann::json::array()))
        {
            const auto id = item.value("id", std::string {});
            if (!id.empty())
                ids.push_back(id);
        }
        return ids;
    }

    std::optional<InstalledSource> installedSource(const std::filesystem::path& projectRoot,
                                                   const std::string&           pluginId)
    {
        const auto lock = loadLock(projectRoot / kLockFileName);
        for (const auto& item : lock.value("plugins", nlohmann::json::array()))
        {
            if (item.value("id", std::string {}) != pluginId)
                continue;
            const auto source = item.value("source", nlohmann::json::object());
            return InstalledSource {
                .type = source.value("type", std::string {"unknown"}),
                .url  = source.value("url", std::string {}),
                .ref  = source.value("ref", std::string {}),
            };
        }
        return std::nullopt;
    }

    void refreshLock(const std::filesystem::path&    projectRoot,
                     const std::string&              assetRoot,
                     const std::vector<std::string>& excludedIds)
    {
        saveLock(projectRoot, localInstallDir(projectRoot, assetRoot), std::nullopt, nlohmann::json {}, excludedIds);
    }

    void restoreLockedPlugins(const std::filesystem::path& projectRoot)
    {
        namespace fs = std::filesystem;
        const auto lock = loadLock(projectRoot / kLockFileName);
        for (const auto& item : lock.value("plugins", nlohmann::json::array()))
        {
            const auto source = item.value("source", nlohmann::json::object());
            if (source.value("type", std::string {}) != "git")
                continue;
            const auto url       = source.value("url", std::string {});
            const auto directory = item.value("directory", std::string {});
            if (url.empty() || directory.empty())
                continue;

            const auto      versionDir = absoluteLockDir(projectRoot, directory);
            std::error_code ec;
            if (fs::exists(versionDir / vultra::kPluginManifestFile, ec))
                continue;

            const auto  ref      = source.value("ref", std::string {});
            const auto  cacheDir = (gitCacheRoot(projectRoot) / net::readableRepoName(url)).lexically_normal();
            std::string cacheWarning;
            std::string status;
            if (!git::syncCache(cacheDir, url, ref, cacheHoldsPlugin, cacheWarning, status))
            {
                VULTRA_CLIENT_WARN("[PluginRepository] Cannot restore locked plugin '{}': {}",
                                   item.value("id", std::string {"?"}),
                                   status);
                continue;
            }
            const auto root = findPluginRoot(cacheDir);
            if (!root.has_value())
                continue;
            std::string copyError;
            if (!git::copyPayloadStripGit(*root, versionDir, copyError))
                VULTRA_CLIENT_WARN("[PluginRepository] Cannot restore locked plugin '{}': {}",
                                   item.value("id", std::string {"?"}),
                                   copyError);
        }
    }

    bool fetchCatalog(const std::filesystem::path& projectRoot,
                      const std::string&           location,
                      std::vector<CatalogEntry>&   entries,
                      std::string&                 status)
    {
        namespace fs = std::filesystem;
        entries.clear();
        if (location.empty())
        {
            status = "Catalog location is empty.";
            return false;
        }

        std::error_code ec;
        fs::path        catalogPath {location};
        std::string     offlineNote;
        if (net::isHttpUrl(location))
        {
            catalogPath =
                (managedRoot(projectRoot) / "catalogs" / (net::readableRepoName(location) + ".json"))
                    .lexically_normal();
            // CDN hosts (raw.githubusercontent.com caches ~5 minutes) key their cache on the full
            // URL, so a throwaway query parameter makes a refresh actually fetch fresh content.
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
            status = "Plugin catalog could not be opened.";
            return false;
        }

        auto json = nlohmann::json::parse(file, nullptr, false);
        if (json.is_discarded() || !json.is_object() || !json.value("plugins", nlohmann::json::array()).is_array())
        {
            status = "Plugin catalog is not valid JSON.";
            return false;
        }

        for (const auto& plugin : json.value("plugins", nlohmann::json::array()))
        {
            auto versions = parseCatalogVersions(plugin);
            if (versions.empty())
                continue;

            CatalogEntry entry;
            entry.id          = plugin.value("id", std::string {});
            entry.name        = plugin.value("name", std::string {});
            entry.author      = plugin.value("author", std::string {});
            entry.description = plugin.value("description", std::string {});
            entry.repository  = plugin.value("repository", std::string {});
            if (const auto it = plugin.find("platforms"); it != plugin.end() && it->is_array())
            {
                for (const auto& p : *it)
                    if (p.is_string())
                        entry.platforms.push_back(p.get<std::string>());
            }
            entry.versions = std::move(versions);
            entries.push_back(std::move(entry));
        }

        if (entries.empty())
        {
            status = "Catalog did not contain any git-backed plugins.";
            return false;
        }

        status = "Loaded " + std::to_string(entries.size()) + " catalog plugin(s)." + offlineNote;
        return true;
    }

    int compareVersions(const std::string& a, const std::string& b)
    {
        const auto nextComponent = [](const std::string& text, std::size_t& pos) {
            unsigned long value = 0;
            bool          any   = false;
            while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos])))
            {
                value = value * 10 + static_cast<unsigned long>(text[pos] - '0');
                any   = true;
                ++pos;
            }
            if (pos < text.size() && !std::isdigit(static_cast<unsigned char>(text[pos])))
                ++pos; // skip one separator/suffix character
            return any ? static_cast<long>(value) : -1L;
        };

        std::size_t posA = 0;
        std::size_t posB = 0;
        while (posA < a.size() || posB < b.size())
        {
            const long componentA = posA < a.size() ? nextComponent(a, posA) : 0;
            const long componentB = posB < b.size() ? nextComponent(b, posB) : 0;
            if (componentA != componentB)
                return componentA < componentB ? -1 : 1;
        }
        return 0;
    }

    ImportResult importFromGit(const std::filesystem::path& projectRoot,
                               const std::filesystem::path& pluginsDir,
                               const std::string&           url,
                               const std::string&           ref)
    {
        namespace fs = std::filesystem;
        ImportResult result;
        if (url.empty())
        {
            result.status = "Git URL is empty.";
            return result;
        }

        const auto      cacheDir = (gitCacheRoot(projectRoot) / net::readableRepoName(url)).lexically_normal();
        std::error_code ec;
        fs::create_directories(cacheDir.parent_path(), ec);
        if (ec)
        {
            result.status = "Failed to create git plugin cache: " + ec.message();
            return result;
        }

        std::string cacheWarning;
        if (!git::syncCache(cacheDir, url, ref, cacheHoldsPlugin, cacheWarning, result.status))
        {
            result.status = "Git import failed: " + result.status;
            return result;
        }

        const auto root = findPluginRoot(cacheDir);
        if (!root.has_value())
        {
            result.status = "Git repository did not contain " + std::string(vultra::kPluginManifestFile) + ".";
            return result;
        }

        std::string manifestError;
        auto        manifest = vultra::loadPluginManifest(*root / vultra::kPluginManifestFile, &manifestError);
        if (!manifest.has_value())
        {
            result.status = manifestError.empty() ? "git plugin manifest could not be read" : manifestError;
            return result;
        }

        // Materialize the payload as an immutable version directory (xmake-repo style):
        // <managed-root>/<id>/<version>. Rollback re-points the lock at a sibling version.
        const auto version =
            !manifest->version.empty() ? manifest->version : (ref.empty() ? std::string {"dev"} : ref);
        const auto  versionDir = (managedRoot(projectRoot) / manifest->id / version).lexically_normal();
        std::string copyError;
        if (!git::copyPayloadStripGit(*root, versionDir, copyError))
        {
            result.status = "Git import failed: " + copyError;
            return result;
        }

        auto installedManifest = vultra::loadPluginManifest(versionDir / vultra::kPluginManifestFile, &manifestError);
        if (!installedManifest.has_value())
        {
            result.status = manifestError.empty() ? "installed plugin manifest could not be read" : manifestError;
            return result;
        }

        nlohmann::json source {
            {"type", "git"},
            {"url", url},
            {"cache", fs::relative(cacheDir, projectRoot, ec).generic_string()},
        };
        if (!ref.empty())
            source["ref"] = ref;

        saveLock(projectRoot, pluginsDir, installedManifest, source);
        result.ok          = true;
        result.installedId = installedManifest->id;
        result.status      = "Installed managed plugin '" + installedManifest->name + "' (" + installedManifest->id +
                        ", v" + version + ")." + cacheWarning;
        return result;
    }

    ImportResult importFromZip(const std::filesystem::path& projectRoot,
                               const std::filesystem::path& pluginsDir,
                               const std::filesystem::path& zipPath)
    {
        namespace fs = std::filesystem;
        ImportResult    result;
        std::error_code ec;
        if (!fs::exists(zipPath, ec) || !fs::is_regular_file(zipPath, ec))
        {
            result.status = "Zip file does not exist.";
            return result;
        }

        const auto extractDir =
            (projectRoot / ".vultra" / "plugins" / "zip-imports" / net::safeCacheName(zipPath.stem().generic_string()))
                .lexically_normal();
        fs::remove_all(extractDir, ec);
        fs::create_directories(extractDir, ec);
        if (ec)
        {
            result.status = "Failed to create zip import cache: " + ec.message();
            return result;
        }

        std::ostringstream cmd;
#if defined(_WIN32)
        cmd << "tar -xf " << net::quoteCommandArg(zipPath.generic_string()) << " -C "
            << net::quoteCommandArg(extractDir.generic_string());
#else
        cmd << "unzip -o " << net::quoteCommandArg(zipPath.generic_string()) << " -d "
            << net::quoteCommandArg(extractDir.generic_string());
#endif
        if (std::system(cmd.str().c_str()) != 0)
        {
            result.status = "Failed to extract plugin zip.";
            return result;
        }

        const auto root = findPluginRoot(extractDir);
        if (!root.has_value())
        {
            result.status = "Zip did not contain " + std::string(vultra::kPluginManifestFile) + ".";
            return result;
        }

        return installLocalPluginFromRoot(
            projectRoot, pluginsDir, *root, nlohmann::json {{"type", "zip"}, {"path", zipPath.generic_string()}});
    }

    ImportResult importFromFolder(const std::filesystem::path& projectRoot,
                                  const std::filesystem::path& pluginsDir,
                                  const std::filesystem::path& sourceFolder)
    {
        ImportResult result;
        const auto   root = findPluginRoot(sourceFolder);
        if (!root.has_value())
        {
            result.status = "No " + std::string(vultra::kPluginManifestFile) + " found in folder.";
            return result;
        }

        return installLocalPluginFromRoot(
            projectRoot,
            pluginsDir,
            *root,
            nlohmann::json {{"type", "folder"}, {"path", sourceFolder.generic_string()}});
    }

    bool removeInstall(const std::filesystem::path&  projectRoot,
                       const std::string&            assetRoot,
                       const vultra::PluginManifest& manifest,
                       std::string&                  status)
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        if (manifest.directory.empty() || !fs::exists(manifest.directory, ec))
        {
            status = "Plugin directory does not exist.";
            return false;
        }

        const auto isUnder = [&ec](const fs::path& path, const fs::path& base) {
            ec.clear();
            const auto rel  = fs::relative(path, base, ec);
            const auto text = rel.generic_string();
            return !ec && !text.empty() && text != ".." && !text.starts_with("../");
        };

        const auto localDir   = localInstallDir(projectRoot, assetRoot).lexically_normal();
        const auto managedDir = managedRoot(projectRoot).lexically_normal();
        const auto pluginDir  = manifest.directory.lexically_normal();
        const bool inLocal    = isUnder(pluginDir, localDir);
        const bool inManaged  = !inLocal && isUnder(pluginDir, managedDir);
        if (!inLocal && !inManaged)
        {
            status = "Refusing to remove a plugin outside the project plugin roots.";
            return false;
        }

        if (inLocal)
        {
            fs::remove_all(pluginDir, ec);
            if (ec)
            {
                status = "Failed to remove plugin: " + ec.message();
                return false;
            }
            status = "Removed local plugin '" + manifest.name + "' (" + manifest.id + ").";
        }
        else
        {
            // Managed layout is <managed-root>/<id>/<version>; drop every materialized version of
            // the plugin. Legacy single-folder entries (and anything else) only drop the version
            // folder itself. The git cache under .cache/ always stays.
            auto removeDir = pluginDir;
            if (pluginDir.parent_path().filename().generic_string() == manifest.id &&
                pluginDir.parent_path().parent_path() == managedDir)
                removeDir = pluginDir.parent_path();
            fs::remove_all(removeDir, ec);
            if (ec)
            {
                status = "Failed to remove plugin: " + ec.message();
                return false;
            }
            status = "Removed managed plugin '" + manifest.name + "' (" + manifest.id + ").";
        }

        refreshLock(projectRoot, assetRoot, {manifest.id});
        return true;
    }
} // namespace vultra_app::plugins
