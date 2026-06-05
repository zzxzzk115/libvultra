#include "vultra/function/plugin/plugin_system.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/engine/engine_context.hpp"
#include "vultra/core/plugin/plugin_manager.hpp"
#include "vultra/function/services/script_service.hpp"

#include <nlohmann/json.hpp>
#include <sol/sol.hpp>

#include <algorithm>
#include <fstream>
#include <system_error>

namespace vultra
{
    // --- PluginManifest -----------------------------------------------------------------------

    std::string_view currentPluginPlatform()
    {
#if defined(__EMSCRIPTEN__)
        return "wasm";
#elif defined(__ANDROID__)
        return "android";
#elif defined(_WIN32)
        return "windows";
#elif defined(__APPLE__)
        return "macos";
#else
        return "linux";
#endif
    }

    bool PluginManifest::supportsPlatform(std::string_view platform) const
    {
        if (platforms.empty())
            return true;
        return std::any_of(platforms.begin(), platforms.end(), [&](const std::string& p) { return p == platform; });
    }

    bool PluginManifest::supportsCurrentPlatform() const { return supportsPlatform(currentPluginPlatform()); }

    std::optional<PluginManifest> loadPluginManifest(const std::filesystem::path& manifestPath, std::string* error)
    {
        const auto setError = [&](std::string message) {
            if (error != nullptr)
                *error = std::move(message);
            return std::nullopt;
        };

        std::ifstream file(manifestPath);
        if (!file)
            return setError("cannot open manifest: " + manifestPath.generic_string());

        nlohmann::json json;
        try
        {
            file >> json;
        }
        catch (const std::exception& e)
        {
            return setError("invalid JSON in " + manifestPath.generic_string() + ": " + e.what());
        }
        if (!json.is_object())
            return setError("manifest must be a JSON object: " + manifestPath.generic_string());

        PluginManifest manifest;
        manifest.manifestPath = manifestPath;
        manifest.directory    = manifestPath.parent_path();
        manifest.id           = json.value("id", std::string {});
        manifest.name         = json.value("name", std::string {});
        manifest.version      = json.value("version", std::string {});
        manifest.author       = json.value("author", std::string {});
        manifest.description  = json.value("description", std::string {});
        manifest.readme       = json.value("readme", std::string {});
        manifest.repository   = json.value("repository", std::string {});
        manifest.native       = json.value("native", std::string {});
        manifest.entry        = json.value("entry", std::string {});
        if (const auto it = json.find("platforms"); it != json.end() && it->is_array())
        {
            for (const auto& p : *it)
                if (p.is_string())
                    manifest.platforms.push_back(p.get<std::string>());
        }

        if (manifest.id.empty())
            manifest.id = manifest.directory.filename().generic_string();
        if (manifest.name.empty())
            manifest.name = manifest.id;
        return manifest;
    }

    std::vector<PluginManifest> discoverPlugins(const std::filesystem::path& dir)
    {
        namespace fs = std::filesystem;
        std::vector<PluginManifest> result;
        std::error_code             ec;
        if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec))
            return result;

        for (const auto& entry : fs::directory_iterator(dir, ec))
        {
            if (ec)
                break;
            if (!entry.is_directory())
                continue;
            const auto manifestPath = entry.path() / kPluginManifestFile;
            if (!fs::exists(manifestPath, ec))
                continue;
            std::string parseError;
            if (auto manifest = loadPluginManifest(manifestPath, &parseError); manifest.has_value())
                result.push_back(std::move(*manifest));
            else
                VULTRA_CORE_WARN("[PluginSystem] Skipping plugin: {}", parseError);
        }
        std::sort(result.begin(), result.end(), [](const PluginManifest& a, const PluginManifest& b) {
            return a.name < b.name;
        });
        return result;
    }

    // --- PluginSystem -------------------------------------------------------------------------

    namespace
    {
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

        const std::filesystem::path dir = ctx().config.plugin.directory;
        const auto&                 enabled = ctx().config.plugin.enabled;
        if (dir.empty() || enabled.empty())
        {
            VULTRA_CORE_INFO("[PluginSystem] No plugins enabled.");
            return true;
        }

        const auto  manifests = discover(dir);
        std::size_t loaded     = 0;
        for (const auto& manifest : manifests)
        {
            if (std::find(enabled.begin(), enabled.end(), manifest.id) == enabled.end())
                continue;
            if (!manifest.supportsCurrentPlatform())
            {
                VULTRA_CORE_WARN("[PluginSystem] Plugin '{}' does not support platform '{}'; skipping.",
                                 manifest.id, currentPluginPlatform());
                continue;
            }
            if (loadPlugin(manifest))
                ++loaded;
        }
        VULTRA_CORE_INFO(
            "[PluginSystem] Loaded {} of {} enabled plugin(s) from '{}'", loaded, enabled.size(), dir.generic_string());
        return true;
    }

    void PluginSystem::onShutdown()
    {
        // Uninstall Lua plugins in reverse load order, while the Lua state is still alive.
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

    std::vector<PluginManifest> PluginSystem::discover(const std::filesystem::path& dir) const
    {
        return discoverPlugins(dir);
    }

    std::vector<std::string> PluginSystem::loadedPlugins() const { return m_LoadedIds; }

    bool PluginSystem::isLoaded(const std::string& id) const
    {
        return std::find(m_LoadedIds.begin(), m_LoadedIds.end(), id) != m_LoadedIds.end();
    }

    bool PluginSystem::loadPlugin(const PluginManifest& manifest)
    {
        if (isLoaded(manifest.id))
            return true;

        // --- Native library first, so its install() can register Lua glue ---------------------
        if (!manifest.native.empty())
        {
            const auto nativePath = resolveNativeLibrary((manifest.directory / manifest.native).lexically_normal());
            if (ctx().pluginManager == nullptr || !ctx().pluginManager->load(nativePath.generic_string(), ctx()))
            {
                VULTRA_CORE_ERROR(
                    "[PluginSystem] '{}': failed to load native library '{}'", manifest.id, nativePath.generic_string());
                return false;
            }
            VULTRA_CORE_INFO("[PluginSystem] '{}': native library loaded ({})", manifest.id, manifest.native);
        }

        // --- Lua entry script -----------------------------------------------------------------
        if (!manifest.entry.empty())
        {
            if (m_Script == nullptr || m_Script->luaState() == nullptr)
            {
                VULTRA_CORE_ERROR("[PluginSystem] '{}': scripting runtime unavailable for Lua entry.", manifest.id);
                return false;
            }
            sol::state_view lua(m_Script->luaState());
            const auto      entryPath = (manifest.directory / manifest.entry).lexically_normal();
            auto            result    = lua.safe_script_file(entryPath.generic_string(), &sol::script_pass_on_error);
            if (!result.valid())
            {
                sol::error err = result;
                VULTRA_CORE_ERROR(
                    "[PluginSystem] '{}': entry error in '{}': {}", manifest.id, entryPath.generic_string(), err.what());
                return false;
            }

            auto        plugin = std::make_unique<LuaPlugin>();
            plugin->name       = manifest.name;
            sol::object obj    = result;
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
                        VULTRA_CORE_ERROR("[PluginSystem] '{}': on_install error: {}", manifest.id, err.what());
                    }
                }
            }
            m_LuaPlugins.push_back(std::move(plugin));
            VULTRA_CORE_INFO("[PluginSystem] '{}': Lua entry loaded ({})", manifest.id, manifest.entry);
        }

        m_LoadedIds.push_back(manifest.id);
        VULTRA_CORE_INFO("[PluginSystem] Plugin '{}' ({}) installed.", manifest.name, manifest.id);
        return true;
    }
} // namespace vultra
