#include "vultra/function/plugin/plugin_system.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/engine/engine_context.hpp"
#include "vultra/core/plugin/plugin_manager.hpp"
#include "vultra/function/services/script_service.hpp"

#include <sol/sol.hpp>

#include <system_error>

namespace vultra
{
    namespace
    {
        // Append the platform's shared-library extension when the manifest gives a bare name, so a
        // single `native = "my_plugin"` manifest works across Windows/Linux/macOS.
        std::filesystem::path resolveNativeLibrary(const std::filesystem::path& path)
        {
            if (path.has_extension())
                return path;
#if defined(_WIN32)
            return std::filesystem::path {path}.replace_extension(".dll");
#elif defined(__APPLE__)
            return std::filesystem::path {path}.replace_extension(".dylib");
#else
            return std::filesystem::path {path}.replace_extension(".so");
#endif
        }
    } // namespace

    struct PluginSystem::LuaPlugin
    {
        std::string name;
        sol::table  module;
    };

    PluginSystem::PluginSystem()  = default;
    PluginSystem::~PluginSystem() = default;

    bool PluginSystem::onInit()
    {
        ctx().services.provide<IPluginService>(this);
        m_Script = ctx().services.tryGet<IScriptService>();

        const auto& dir = ctx().config.plugin.directory;
        if (!dir.empty())
        {
            const auto count = loadPluginsFromDirectory(dir);
            if (count > 0)
                VULTRA_CORE_INFO("[PluginSystem] Loaded {} plugin(s) from '{}'", count, dir);
        }
        return true;
    }

    void PluginSystem::onShutdown()
    {
        // Uninstall Lua plugins in reverse load order, while the Lua state is still alive
        // (PluginSystem shuts down before ScriptSystem because it is emplaced after it).
        for (auto it = m_LuaPlugins.rbegin(); it != m_LuaPlugins.rend(); ++it)
        {
            auto& plugin = **it;
            if (!plugin.module.valid())
                continue;
            sol::protected_function fn = plugin.module["on_uninstall"];
            if (!fn.valid())
                continue;
            auto r = fn();
            if (!r.valid())
            {
                sol::error err = r;
                VULTRA_CORE_ERROR("[PluginSystem] '{}' on_uninstall error: {}", plugin.name, err.what());
            }
        }
        m_LuaPlugins.clear();
        // Native plugins are released by the engine's PluginManager on engine shutdown.
    }

    std::vector<std::string> PluginSystem::loadedPlugins() const { return m_Names; }

    std::size_t PluginSystem::loadPluginsFromDirectory(const std::filesystem::path& dir)
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec))
            return 0;

        std::size_t loaded = 0;
        for (const auto& entry : fs::directory_iterator(dir, ec))
        {
            if (ec)
                break;
            if (!entry.is_directory())
                continue;
            const auto manifest = entry.path() / "plugin.lua";
            if (fs::exists(manifest, ec) && loadPlugin(manifest))
                ++loaded;
        }
        return loaded;
    }

    bool PluginSystem::loadPlugin(const std::filesystem::path& manifestOrDir)
    {
        namespace fs = std::filesystem;
        std::error_code ec;

        fs::path manifest = manifestOrDir;
        if (fs::is_directory(manifestOrDir, ec))
            manifest = manifestOrDir / "plugin.lua";
        if (!fs::exists(manifest, ec))
        {
            VULTRA_CORE_ERROR("[PluginSystem] Manifest not found: {}", manifest.generic_string());
            return false;
        }
        const fs::path baseDir = manifest.parent_path();

        if (m_Script == nullptr || m_Script->luaState() == nullptr)
        {
            VULTRA_CORE_ERROR("[PluginSystem] Scripting runtime unavailable; cannot load plugins.");
            return false;
        }
        sol::state_view lua(m_Script->luaState());

        // --- Parse the manifest table ---------------------------------------------------------
        sol::table manifestTable;
        {
            auto result = lua.safe_script_file(manifest.generic_string(), &sol::script_pass_on_error);
            if (!result.valid())
            {
                sol::error err = result;
                VULTRA_CORE_ERROR("[PluginSystem] Manifest error in '{}': {}", manifest.generic_string(), err.what());
                return false;
            }
            sol::object obj = result;
            if (!obj.is<sol::table>())
            {
                VULTRA_CORE_ERROR("[PluginSystem] Manifest '{}' must return a table.", manifest.generic_string());
                return false;
            }
            manifestTable = obj.as<sol::table>();
        }

        const std::string name      = manifestTable.get_or("name", baseDir.filename().generic_string());
        const std::string nativeRel = manifestTable.get_or("native", std::string {});
        const std::string entryRel  = manifestTable.get_or("entry", std::string {});

        // --- Native library first, so its install() can register Lua glue ---------------------
        if (!nativeRel.empty())
        {
            const auto nativePath = resolveNativeLibrary((baseDir / nativeRel).lexically_normal());
            if (ctx().pluginManager == nullptr || !ctx().pluginManager->load(nativePath.generic_string(), ctx()))
            {
                VULTRA_CORE_ERROR(
                    "[PluginSystem] '{}': failed to load native library '{}'", name, nativePath.generic_string());
                return false;
            }
            VULTRA_CORE_INFO("[PluginSystem] '{}': native library loaded ({})", name, nativeRel);
        }

        // --- Lua entry script -----------------------------------------------------------------
        if (!entryRel.empty())
        {
            const auto entryPath = (baseDir / entryRel).lexically_normal();
            auto       result    = lua.safe_script_file(entryPath.generic_string(), &sol::script_pass_on_error);
            if (!result.valid())
            {
                sol::error err = result;
                VULTRA_CORE_ERROR(
                    "[PluginSystem] '{}': entry error in '{}': {}", name, entryPath.generic_string(), err.what());
                return false;
            }

            auto       plugin = std::make_unique<LuaPlugin>();
            plugin->name      = name;
            sol::object obj   = result;
            if (obj.is<sol::table>())
            {
                plugin->module                    = obj.as<sol::table>();
                sol::protected_function onInstall = plugin->module["on_install"];
                if (onInstall.valid())
                {
                    auto r = onInstall();
                    if (!r.valid())
                    {
                        sol::error err = r;
                        VULTRA_CORE_ERROR("[PluginSystem] '{}': on_install error: {}", name, err.what());
                    }
                }
            }
            m_LuaPlugins.push_back(std::move(plugin));
            VULTRA_CORE_INFO("[PluginSystem] '{}': Lua entry loaded ({})", name, entryRel);
        }

        m_Names.push_back(name);
        VULTRA_CORE_INFO("[PluginSystem] Plugin '{}' installed.", name);
        return true;
    }
} // namespace vultra
