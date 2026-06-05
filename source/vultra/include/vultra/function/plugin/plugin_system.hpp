#pragma once

#include "vultra/core/engine/engine_subsystem.hpp"
#include "vultra/function/services/plugin_service.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace vultra
{
    class IScriptService;

    // Orchestrates plugin discovery and loading on top of the core (native) PluginManager and the
    // Lua scripting runtime. At init it discovers plugins under config.plugin.directory and loads
    // only those whose id is in config.plugin.enabled and that support the current platform.
    class PluginSystem final : public EngineSubsystem, public IPluginService
    {
    public:
        ENGINE_SUBSYSTEM(PluginSystem)

        PluginSystem();
        ~PluginSystem() override;

        bool onInit() override;
        void onShutdown() override;

        std::vector<PluginManifest> discover(const std::filesystem::path& dir) const override;
        bool                        loadPlugin(const PluginManifest& manifest) override;
        std::vector<std::string>    loadedPlugins() const override;
        bool                        isLoaded(const std::string& id) const override;

    private:
        struct LuaPlugin; // defined in the .cpp (holds sol objects)

        // Load a plugin whose files live in the mounted asset VFS at `dirUri` (e.g.
        // "res://plugins/hello"): the Lua entry is read and run from the VFS, and a native library is
        // extracted to a writable dir before being loaded (a shared library cannot be loaded from
        // inside the VPK in place).
        bool loadPluginFromVfs(const PluginManifest& manifest, const std::string& dirUri);
        // Run a plugin's Lua entry from already-loaded source text and register its module.
        bool installLuaEntry(const PluginManifest& manifest, std::string_view source, std::string_view debugName);

        IScriptService*                         m_Script {nullptr};
        std::vector<std::string>                m_LoadedIds;
        std::vector<std::unique_ptr<LuaPlugin>> m_LuaPlugins;
    };
} // namespace vultra
