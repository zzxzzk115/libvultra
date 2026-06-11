#include "editor_app/editor_plugin_manager.hpp"

#include <chrono>

namespace vultra_app
{
    void EditorPluginManager::openProject(const std::filesystem::path& projectRoot, const std::string& assetRoot)
    {
        m_ProjectRoot = projectRoot;
        m_AssetRoot   = assetRoot;
        m_PluginsDir  = plugins::localInstallDir(projectRoot, assetRoot);

        m_InstalledDirty = true;
        if (!projectRoot.empty())
            refreshCatalog();
    }

    void EditorPluginManager::update()
    {
        // Finish a pending catalog fetch.
        if (m_CatalogFetching && m_CatalogFuture.valid() &&
            m_CatalogFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        {
            auto result       = m_CatalogFuture.get();
            m_CatalogFetching = false;
            m_CatalogStatus   = std::move(result.status);
            if (result.ok)
                m_CatalogEntries = std::move(result.entries);
        }

        // Start a queued catalog fetch (one at a time).
        if (m_CatalogFetchQueued && !m_CatalogFetching && !m_ProjectRoot.empty())
        {
            m_CatalogFetchQueued = false;
            m_CatalogFetching    = true;
            const std::string location =
                m_CatalogLocation.empty() ? plugins::defaultCatalogUrl() : m_CatalogLocation;
            m_CatalogFuture = std::async(std::launch::async, [root = m_ProjectRoot, location] {
                CatalogFetchResult result;
                result.ok = plugins::fetchCatalog(root, location, result.entries, result.status);
                return result;
            });
        }

        // Finish a pending import.
        if (m_Importing && m_ImportFuture.valid() &&
            m_ImportFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        {
            auto result      = m_ImportFuture.get();
            m_Importing      = false;
            m_ImportStatus   = result.status;
            m_FinishedImport = std::move(result);
            m_InstalledDirty = true;
        }

        // Refresh the installed scan when stale -- but never while an import is rewriting files
        // in the managed cache, or the scan could read a manifest mid-checkout.
        if (m_InstalledDirty && !m_Importing && !m_ProjectRoot.empty())
        {
            m_Installed = plugins::discoverProjectPlugins(plugins::discoveryDirs(m_ProjectRoot, m_AssetRoot));
            m_InstalledDirty = false;
        }
    }

    const vultra::PluginManifest* EditorPluginManager::findInstalled(const std::string& id) const
    {
        for (const auto& manifest : m_Installed)
        {
            if (manifest.id == id)
                return &manifest;
        }
        return nullptr;
    }

    void EditorPluginManager::setCatalogLocation(std::string location)
    {
        m_CatalogLocation = std::move(location);
    }

    const plugins::CatalogEntry* EditorPluginManager::findCatalogEntry(const std::string& id) const
    {
        if (id.empty())
            return nullptr;
        for (const auto& entry : m_CatalogEntries)
        {
            if (entry.id == id)
                return &entry;
        }
        return nullptr;
    }

    std::optional<plugins::ImportResult> EditorPluginManager::takeFinishedImport()
    {
        auto result = std::move(m_FinishedImport);
        m_FinishedImport.reset();
        return result;
    }

    void EditorPluginManager::startImport(std::function<plugins::ImportResult()> task)
    {
        if (m_Importing || m_ProjectRoot.empty())
            return;
        m_Importing    = true;
        m_ImportStatus.clear();
        m_ImportFuture = std::async(std::launch::async, std::move(task));
    }

    void EditorPluginManager::installFromCatalog(const plugins::CatalogVersion& version)
    {
        startImport([root = m_ProjectRoot, dir = m_PluginsDir, url = version.gitUrl, ref = version.gitRef] {
            return plugins::importFromGit(root, dir, url, ref);
        });
    }

    void EditorPluginManager::importFromGit(const std::string& url)
    {
        startImport([root = m_ProjectRoot, dir = m_PluginsDir, url] {
            return plugins::importFromGit(root, dir, url);
        });
    }

    void EditorPluginManager::importFromZip(const std::filesystem::path& zipPath)
    {
        startImport([root = m_ProjectRoot, dir = m_PluginsDir, zipPath] {
            return plugins::importFromZip(root, dir, zipPath);
        });
    }

    void EditorPluginManager::importFromFolder(const std::filesystem::path& folder)
    {
        startImport([root = m_ProjectRoot, dir = m_PluginsDir, folder] {
            return plugins::importFromFolder(root, dir, folder);
        });
    }

    bool EditorPluginManager::removePlugin(const vultra::PluginManifest& manifest, std::string& status)
    {
        const bool removed = plugins::removeInstall(m_ProjectRoot, m_AssetRoot, manifest, status);
        if (removed)
            m_InstalledDirty = true;
        return removed;
    }
} // namespace vultra_app
