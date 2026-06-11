#pragma once

#include "vultra/function/plugin/plugin_manifest.hpp"

#include <vbase/service/service_registry.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace vultra
{
    // Service for discovering and loading runtime plugins. A plugin can ship a native C++ shared
    // library, a Lua script, or both - the native side typically registers glue bindings (wrapping
    // a third-party library) that the Lua side, or ordinary entity scripts, then drive.
    //
    // Plugins are OFF by default: the engine only loads the ones whose id is in the enabled set
    // (config.plugin.enabled, sourced from the project's settings). Discovery lists everything in a
    // directory so a UI can present and toggle them.
    // Where an active plugin's content lives, for content pipelines (render pass scan, shader
    // lookup, tooling): the physical directory plus the VFS uri it is reachable under
    // (`plugins://<id>/<version>` for managed plugins, `res://plugins/<folder>` for local installs).
    struct PluginContentRoot
    {
        std::string           id;
        std::filesystem::path directory;
        std::string           uri;
    };

    class IPluginService
    {
    public:
        SERVICE_REGISTER(IPluginService)
        virtual ~IPluginService() = default;

        // Discover all plugins under a directory (each `<sub>/vultra.plugin.vmanifest`). No loading.
        virtual std::vector<PluginManifest> discover(const std::filesystem::path& dir) const = 0;

        // Content roots of every installed plugin (content stays visible while a plugin is
        // disabled so render graphs keep authoring/loading; loading governs what actually runs).
        // Empty in packaged (VPK) mode, where plugin content is already part of res://.
        virtual std::vector<PluginContentRoot> contentRoots() = 0;

        // Load a single (already discovered) plugin: native library first, then the Lua entry.
        virtual bool loadPlugin(const PluginManifest& manifest) = 0;

        // Unload a currently loaded plugin: Lua on_uninstall first, then native uninstall and DLL unload.
        virtual bool unloadPlugin(const std::string& id) = 0;

        // Ids of plugins currently loaded, in load order.
        virtual std::vector<std::string> loadedPlugins() const = 0;

        virtual bool isLoaded(const std::string& id) const = 0;
    };
} // namespace vultra
