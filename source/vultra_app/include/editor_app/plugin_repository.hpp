#pragma once

#include <vultra/function/plugin/plugin_manifest.hpp>

#include <filesystem>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>
#include <vector>

// Project plugin repository management: discovery, the vultra.plugins.lock file, catalog
// fetching/parsing (schema v1 and v2), and install/update/rollback of plugins from git, zip,
// or folder sources. No UI here; the Project Settings window drives these and renders results.
//
// Managed plugin store layout (xmake-repo style, mounted as the plugins:// VFS scheme):
//
//   .vultra/plugins/
//     .cache/<owner>-<repo>/        git clone cache, one per repository url
//     catalogs/<owner>-<repo>.json  downloaded catalog cache
//     <plugin-id>/<version>/        immutable, materialized plugin payload (no .git)
//
// `vultra.plugins.lock` records which version directory is active per plugin; rollback just
// re-points the lock at an already-materialized version. Local (zip/folder) imports install
// into `<asset-root>/plugins/<folder>` and are project content.
namespace vultra_app::plugins
{
    // --- Locations ------------------------------------------------------------------------------

    [[nodiscard]] std::string defaultCatalogUrl();

    [[nodiscard]] std::filesystem::path localInstallDir(const std::filesystem::path& projectRoot,
                                                        const std::string&           assetRoot);
    // Root of the managed plugin store: `<project>/.vultra/plugins`.
    [[nodiscard]] std::filesystem::path managedRoot(const std::filesystem::path& projectRoot);

    // --- Discovery / lock -----------------------------------------------------------------------

    // Directories the project's plugins live in: the local install dir plus every locked managed
    // version directory (each of those is itself a plugin root).
    [[nodiscard]] std::vector<std::filesystem::path> discoveryDirs(const std::filesystem::path& projectRoot,
                                                                   const std::string&           assetRoot);

    // Discover manifests across dirs (each entry may be a container of plugin folders or a plugin
    // root itself). Duplicate ids keep the first hit, so list preferred dirs first.
    [[nodiscard]] std::vector<vultra::PluginManifest>
    discoverProjectPlugins(const std::vector<std::filesystem::path>& dirs);

    [[nodiscard]] std::vector<std::string> lockedPluginIds(const std::filesystem::path& projectRoot);

    // Source metadata recorded in vultra.plugins.lock for an installed plugin.
    struct InstalledSource
    {
        std::string type; // "git" | "zip" | "folder" | "unknown"
        std::string url;
        std::string ref;
    };
    [[nodiscard]] std::optional<InstalledSource> installedSource(const std::filesystem::path& projectRoot,
                                                                 const std::string&           pluginId);

    void refreshLock(const std::filesystem::path&    projectRoot,
                     const std::string&              assetRoot,
                     const std::vector<std::string>& excludedIds = {});

    // Re-materialize any locked managed plugin whose version directory is missing (fresh checkout
    // of the locked ref into the git cache, then a payload copy). Called at project load.
    void restoreLockedPlugins(const std::filesystem::path& projectRoot);

    // --- Catalog --------------------------------------------------------------------------------

    struct CatalogVersion
    {
        std::string version;
        std::string notes;
        std::string gitUrl;
        std::string gitRef;
        // Lowest engine version this plugin version supports (catalog "minEngineVersion", per-version
        // or inherited from the entry). Empty = no declared minimum. Drives the install gate.
        std::string minEngineVersion;
    };

    // The running engine version (VULTRA_ENGINE_VERSION), e.g. "0.1.0".
    [[nodiscard]] std::string engineVersion();

    // True when the running engine satisfies minEngineVersion (empty minimum = always true).
    [[nodiscard]] bool engineSupports(const std::string& minEngineVersion);

    struct CatalogEntry
    {
        std::string                 id;
        std::string                 name;
        std::string                 author;
        std::string                 description;
        std::string                 repository;
        std::vector<std::string>    platforms;
        std::vector<CatalogVersion> versions; // newest first; [0] is the latest release
    };

    // Fetch and parse a catalog from an http(s) URL or a local path. Accepts schema v1 (single
    // version/source per plugin) and v2 (versions[] per plugin). Remote downloads are cached
    // under the managed store's catalogs/ folder; on download failure a previously cached copy is
    // used so the catalog still renders offline.
    bool fetchCatalog(const std::filesystem::path& projectRoot,
                      const std::string&           location,
                      std::vector<CatalogEntry>&   entries,
                      std::string&                 status);

    // Compare dotted version strings numerically ("0.10.0" > "0.9.1"). Returns <0, 0, >0.
    [[nodiscard]] int compareVersions(const std::string& a, const std::string& b);

    // --- Install / update / remove ----------------------------------------------------------------

    struct ImportResult
    {
        bool        ok {false};
        std::string status;
        std::string installedId;
    };

    // Sync the repository's git cache to `ref` (a release tag; empty tracks the default branch)
    // and materialize the payload into `<managed-root>/<id>/<version>/`. Catalog installs,
    // updates, and rollbacks all run through this.
    [[nodiscard]] ImportResult importFromGit(const std::filesystem::path& projectRoot,
                                             const std::filesystem::path& pluginsDir,
                                             const std::string&           url,
                                             const std::string&           ref = {});

    [[nodiscard]] ImportResult importFromZip(const std::filesystem::path& projectRoot,
                                             const std::filesystem::path& pluginsDir,
                                             const std::filesystem::path& zipPath);

    [[nodiscard]] ImportResult importFromFolder(const std::filesystem::path& projectRoot,
                                                const std::filesystem::path& pluginsDir,
                                                const std::filesystem::path& sourceFolder);

    // Remove an installed plugin: local installs are deleted from <asset-root>/plugins; managed
    // installs drop their `<managed-root>/<id>/` versions and the lock entry (the git cache stays).
    bool removeInstall(const std::filesystem::path&  projectRoot,
                       const std::string&            assetRoot,
                       const vultra::PluginManifest& manifest,
                       std::string&                  status);
} // namespace vultra_app::plugins
