#pragma once

#include <vbase/service/service_registry.hpp>

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace vultra
{
    // Service for loading runtime plugins. A plugin can ship a native C++ shared library, a Lua
    // script, or both — the native side typically registers glue bindings (wrapping a third-party
    // library) that the Lua side, or ordinary entity scripts, then drive.
    class IPluginService
    {
    public:
        SERVICE_REGISTER(IPluginService)
        virtual ~IPluginService() = default;

        // Load a single plugin from its manifest file (`plugin.lua`) or its containing directory.
        virtual bool loadPlugin(const std::filesystem::path& manifestOrDir) = 0;

        // Scan a directory for `<name>/plugin.lua` manifests and load each one. Returns the count
        // of plugins successfully loaded. A missing directory is not an error (returns 0).
        virtual std::size_t loadPluginsFromDirectory(const std::filesystem::path& dir) = 0;

        // Names of the plugins currently loaded, in load order.
        virtual std::vector<std::string> loadedPlugins() const = 0;
    };
} // namespace vultra
