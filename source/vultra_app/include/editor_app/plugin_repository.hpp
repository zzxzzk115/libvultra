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
namespace vultra_app::plugins
{
    // --- Locations ------------------------------------------------------------------------------

    [[nodiscard]] std::string defaultCatalogUrl();

    [[nodiscard]] std::filesystem::path localInstallDir(const std::filesystem::path& projectRoot,
                                                        const std::string&           assetRoot);
    [[nodiscard]] std::filesystem::path managedGitDir(const std::filesystem::path& projectRoot);
    [[nodiscard]] std::vector<std::filesystem::path> discoveryDirs(const std::filesystem::path& projectRoot,
                                                                   const std::string&           assetRoot);

    // --- Discovery / lock -----------------------------------------------------------------------

    // Discover manifests across dirs. Managed-cache checkouts (under .vultra/plugins/git) are only
    // reported when their id is in lockedManagedIds, unless includeAllManaged is set.
    [[nodiscard]] std::vector<vultra::PluginManifest>
    discoverProjectPlugins(const std::vector<std::filesystem::path>& dirs,
                           const std::vector<std::string>&           lockedManagedIds = {},
                           bool                                      includeAllManaged = false);

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

    // True when any of the project's enabled plugins must be loaded before the render device
    // exists (needsRestartToApply, e.g. a pre-render-device Vulkan bridge). Opening such a project
    // requires a fresh process (--editor --project ...); an in-process launcher transition or a
    // mid-session enable is too late.
    [[nodiscard]] bool projectNeedsRelaunchForPlugins(const std::filesystem::path&    projectRoot,
                                                      const std::string&              assetRoot,
                                                      const std::vector<std::string>& enabledPlugins);

    // --- Catalog --------------------------------------------------------------------------------

    struct CatalogVersion
    {
        std::string version;
        std::string notes;
        std::string gitUrl;
        std::string gitRef;
    };

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
    // under <project>/.vultra/plugins/catalogs; on download failure a previously cached copy is
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

    // Clone (or update a cached checkout of) a git repository into the managed cache and register
    // the contained plugin in the lock. A non-empty ref (tag) selects that exact release, which is
    // how catalog installs, updates, and rollbacks all work; an empty ref tracks the default branch.
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
    // git installs are dropped from the lock (the cache stays reusable).
    bool removeInstall(const std::filesystem::path&  projectRoot,
                       const std::string&            assetRoot,
                       const vultra::PluginManifest& manifest,
                       std::string&                  status);
} // namespace vultra_app::plugins
