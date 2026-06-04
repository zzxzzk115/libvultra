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

    // Orchestrates plugin loading on top of the core (native) PluginManager and the Lua scripting
    // runtime. Each plugin is described by a `plugin.lua` manifest that returns a table:
    //
    //   return {
    //     name   = "my_plugin",          -- optional, defaults to the folder name
    //     native = "libmy_plugin.dll",   -- optional native shared library (relative to manifest)
    //     entry  = "init.lua",           -- optional Lua entry script (relative to manifest)
    //   }
    //
    // The native library (if any) is loaded first so its install() can register glue bindings into
    // the shared Lua state; the Lua entry script is then run and its optional on_install() called.
    class PluginSystem final : public EngineSubsystem, public IPluginService
    {
    public:
        ENGINE_SUBSYSTEM(PluginSystem)

        PluginSystem();
        ~PluginSystem() override;

        bool onInit() override;
        void onShutdown() override;

        bool        loadPlugin(const std::filesystem::path& manifestOrDir) override;
        std::size_t loadPluginsFromDirectory(const std::filesystem::path& dir) override;
        std::vector<std::string> loadedPlugins() const override;

    private:
        struct LuaPlugin; // defined in the .cpp (holds sol objects)

        IScriptService*                         m_Script {nullptr};
        std::vector<std::string>                m_Names;
        std::vector<std::unique_ptr<LuaPlugin>> m_LuaPlugins;
    };
} // namespace vultra
