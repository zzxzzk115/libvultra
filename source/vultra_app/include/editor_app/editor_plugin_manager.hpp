#pragma once

#include "editor_app/plugin_repository.hpp"

#include <filesystem>
#include <functional>
#include <future>
#include <optional>
#include <string>
#include <vector>

namespace vultra_app
{
    // Editor-side plugin management for the open project. Owns the installed-plugin scan cache,
    // the plugin catalog state, and the single in-flight asynchronous install/import task, on top
    // of the stateless repository operations in vultra_app::plugins. UI code renders from the
    // queries and triggers the operations; it keeps no plugin state of its own.
    class EditorPluginManager
    {
    public:
        // Bind to the project the settings dialog is editing. Invalidates the installed scan and
        // schedules a catalog re-fetch. Safe to call while a previous import is still running.
        void openProject(const std::filesystem::path& projectRoot, const std::string& assetRoot);

        // Poll async work and refresh stale caches. Call once per frame while the UI is visible.
        // The installed list is never rescanned while an import is rewriting cache files.
        void update();

        // --- Installed plugins -----------------------------------------------------------------

        [[nodiscard]] const std::vector<vultra::PluginManifest>& installedManifests() const
        {
            return m_Installed;
        }
        [[nodiscard]] const vultra::PluginManifest* findInstalled(const std::string& id) const;
        void                                        invalidateInstalled() { m_InstalledDirty = true; }

        // --- Catalog ---------------------------------------------------------------------------

        // Empty location = the official catalog. A custom location persists until changed.
        void                             setCatalogLocation(std::string location);
        [[nodiscard]] const std::string& catalogLocation() const { return m_CatalogLocation; }
        void                             refreshCatalog() { m_CatalogFetchQueued = true; }
        [[nodiscard]] bool               catalogFetching() const { return m_CatalogFetching; }
        [[nodiscard]] const std::string& catalogStatus() const { return m_CatalogStatus; }
        [[nodiscard]] const std::vector<plugins::CatalogEntry>& catalogEntries() const
        {
            return m_CatalogEntries;
        }
        [[nodiscard]] const plugins::CatalogEntry* findCatalogEntry(const std::string& id) const;

        // --- Asynchronous import / install / update / rollback ----------------------------------

        [[nodiscard]] bool importing() const { return m_Importing; }
        // Repository-layer status of the last finished import (empty while none has finished).
        [[nodiscard]] const std::string& importStatus() const { return m_ImportStatus; }
        // One-shot: the result of an import that finished since the last call.
        [[nodiscard]] std::optional<plugins::ImportResult> takeFinishedImport();

        // Installing a specific catalog version covers install, update, and rollback alike.
        void installFromCatalog(const plugins::CatalogVersion& version);
        void importFromGit(const std::string& url);
        void importFromZip(const std::filesystem::path& zipPath);
        void importFromFolder(const std::filesystem::path& folder);

        // --- Remove (synchronous) ----------------------------------------------------------------

        bool removePlugin(const vultra::PluginManifest& manifest, std::string& status);

    private:
        void startImport(std::function<plugins::ImportResult()> task);

        std::filesystem::path m_ProjectRoot;
        std::string           m_AssetRoot;
        std::filesystem::path m_PluginsDir;

        std::vector<vultra::PluginManifest> m_Installed;
        bool                                m_InstalledDirty {true};

        struct CatalogFetchResult
        {
            bool                               ok {false};
            std::string                        status;
            std::vector<plugins::CatalogEntry> entries;
        };
        std::string                        m_CatalogLocation;
        std::vector<plugins::CatalogEntry> m_CatalogEntries;
        std::string                        m_CatalogStatus;
        bool                               m_CatalogFetching {false};
        bool                               m_CatalogFetchQueued {false};
        std::future<CatalogFetchResult>    m_CatalogFuture;

        bool                                 m_Importing {false};
        std::string                          m_ImportStatus;
        std::future<plugins::ImportResult>   m_ImportFuture;
        std::optional<plugins::ImportResult> m_FinishedImport;
    };
} // namespace vultra_app
