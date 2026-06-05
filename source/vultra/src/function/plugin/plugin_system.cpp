#include "vultra/function/plugin/plugin_system.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/engine/engine_context.hpp"
#include "vultra/core/plugin/plugin_manager.hpp"
#include "vultra/function/services/asset_service.hpp"
#include "vultra/function/services/script_service.hpp"

#include <nlohmann/json.hpp>
#include <sol/sol.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>
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

    // Parse manifest fields from JSON text. Does not set directory/manifestPath or apply id/name
    // fallbacks (the caller does, since those depend on the source location: a disk path or a uri).
    std::optional<PluginManifest> parsePluginManifestText(const std::string& text, std::string* error)
    {
        const auto setError = [&](std::string message) {
            if (error != nullptr)
                *error = std::move(message);
            return std::nullopt;
        };

        nlohmann::json json;
        try
        {
            json = nlohmann::json::parse(text);
        }
        catch (const std::exception& e)
        {
            return setError(std::string {"invalid JSON: "} + e.what());
        }
        if (!json.is_object())
            return setError("manifest must be a JSON object");

        PluginManifest manifest;
        manifest.id          = json.value("id", std::string {});
        manifest.name        = json.value("name", std::string {});
        manifest.version     = json.value("version", std::string {});
        manifest.author      = json.value("author", std::string {});
        manifest.description = json.value("description", std::string {});
        manifest.readme      = json.value("readme", std::string {});
        manifest.repository   = json.value("repository", std::string {});
        manifest.native       = json.value("native", std::string {});
        manifest.entry        = json.value("entry", std::string {});
        if (const auto it = json.find("platforms"); it != json.end() && it->is_array())
        {
            for (const auto& p : *it)
                if (p.is_string())
                    manifest.platforms.push_back(p.get<std::string>());
        }
        return manifest;
    }

    std::optional<PluginManifest> loadPluginManifest(const std::filesystem::path& manifestPath, std::string* error)
    {
        std::ifstream file(manifestPath);
        if (!file)
        {
            if (error != nullptr)
                *error = "cannot open manifest: " + manifestPath.generic_string();
            return std::nullopt;
        }
        std::stringstream buffer;
        buffer << file.rdbuf();

        std::string parseError;
        auto        manifest = parsePluginManifestText(buffer.str(), &parseError);
        if (!manifest.has_value())
        {
            if (error != nullptr)
                *error = parseError + " (" + manifestPath.generic_string() + ")";
            return std::nullopt;
        }

        manifest->manifestPath = manifestPath;
        manifest->directory    = manifestPath.parent_path();
        if (manifest->id.empty())
            manifest->id = manifest->directory.filename().generic_string();
        if (manifest->name.empty())
            manifest->name = manifest->id;
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
        std::string nativeLibrarySuffix()
        {
#if defined(_WIN32)
            return ".dll";
#elif defined(__APPLE__)
            return ".dylib";
#else
            return ".so";
#endif
        }

        std::filesystem::path resolveNativeLibrary(const std::filesystem::path& path)
        {
            if (path.has_extension())
                return path;
            return std::filesystem::path {path}.replace_extension(nativeLibrarySuffix());
        }

        // Last path segment of a uri/dir (e.g. "res://plugins/hello" -> "hello").
        std::string lastPathSegment(const std::string& uri)
        {
            const auto pos = uri.find_last_of('/');
            return pos == std::string::npos ? uri : uri.substr(pos + 1);
        }

        // Writable directory used to extract bundled native libraries before loading them.
        std::filesystem::path pluginExtractionDir(const std::string& writableRoot)
        {
            std::error_code     ec;
            std::filesystem::path base =
                !writableRoot.empty() ? std::filesystem::path {writableRoot} : std::filesystem::temp_directory_path(ec);
            return (base / "vultra_plugins").lexically_normal();
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

        // Packaged mode: load plugins bundled into the asset VPK (read via the mounted res:// VFS).
        // The `packaged` dir uris were pre-filtered to the project's enabled set at export time.
        if (ctx().config.plugin.loadFromVPK)
        {
            const auto& packaged = ctx().config.plugin.packaged;
            if (packaged.empty())
            {
                VULTRA_CORE_INFO("[PluginSystem] No bundled plugins.");
                return true;
            }
            auto* assetService = ctx().services.tryGet<IAssetService>();
            if (assetService == nullptr)
            {
                VULTRA_CORE_WARN("[PluginSystem] Asset service unavailable; cannot load bundled plugins.");
                return true;
            }

            std::size_t loaded = 0;
            for (const auto& dirUri : packaged)
            {
                const std::string manifestUri = dirUri + "/" + kPluginManifestFile;
                auto              text        = assetService->loadTextAssetSync(manifestUri);
                if (!text)
                {
                    VULTRA_CORE_WARN("[PluginSystem] Cannot read bundled manifest '{}'", manifestUri);
                    continue;
                }
                std::string parseError;
                auto        manifest = parsePluginManifestText(text.value(), &parseError);
                if (!manifest.has_value())
                {
                    VULTRA_CORE_WARN("[PluginSystem] Invalid bundled manifest '{}': {}", manifestUri, parseError);
                    continue;
                }
                if (manifest->id.empty())
                    manifest->id = lastPathSegment(dirUri);
                if (manifest->name.empty())
                    manifest->name = manifest->id;
                if (!manifest->supportsCurrentPlatform())
                {
                    VULTRA_CORE_WARN("[PluginSystem] Plugin '{}' does not support platform '{}'; skipping.",
                                     manifest->id, currentPluginPlatform());
                    continue;
                }
                if (loadPluginFromVfs(*manifest, dirUri))
                    ++loaded;
            }
            VULTRA_CORE_INFO("[PluginSystem] Loaded {} of {} bundled plugin(s).", loaded, packaged.size());
            return true;
        }

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
            const auto    entryPath = (manifest.directory / manifest.entry).lexically_normal();
            std::ifstream file(entryPath);
            if (!file)
            {
                VULTRA_CORE_ERROR(
                    "[PluginSystem] '{}': cannot open Lua entry '{}'", manifest.id, entryPath.generic_string());
                return false;
            }
            std::stringstream buffer;
            buffer << file.rdbuf();
            if (!installLuaEntry(manifest, buffer.str(), entryPath.generic_string()))
                return false;
            VULTRA_CORE_INFO("[PluginSystem] '{}': Lua entry loaded ({})", manifest.id, manifest.entry);
        }

        m_LoadedIds.push_back(manifest.id);
        VULTRA_CORE_INFO("[PluginSystem] Plugin '{}' ({}) installed.", manifest.name, manifest.id);
        return true;
    }

    bool PluginSystem::installLuaEntry(const PluginManifest& manifest,
                                       const std::string_view source,
                                       const std::string_view debugName)
    {
        if (m_Script == nullptr || m_Script->luaState() == nullptr)
        {
            VULTRA_CORE_ERROR("[PluginSystem] '{}': scripting runtime unavailable for Lua entry.", manifest.id);
            return false;
        }
        sol::state_view lua(m_Script->luaState());
        auto            result = lua.safe_script(source, &sol::script_pass_on_error);
        if (!result.valid())
        {
            sol::error err = result;
            VULTRA_CORE_ERROR("[PluginSystem] '{}': entry error in '{}': {}", manifest.id, debugName, err.what());
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
        return true;
    }

    bool PluginSystem::loadPluginFromVfs(const PluginManifest& manifest, const std::string& dirUri)
    {
        if (isLoaded(manifest.id))
            return true;

        auto* assetService = ctx().services.tryGet<IAssetService>();
        if (assetService == nullptr)
        {
            VULTRA_CORE_ERROR("[PluginSystem] '{}': asset service unavailable.", manifest.id);
            return false;
        }

        // --- Native library first: extract to a writable dir, then load -------------------------
        if (!manifest.native.empty())
        {
            std::string nativeFile = manifest.native;
            if (std::filesystem::path {nativeFile}.extension().empty())
                nativeFile += nativeLibrarySuffix();
            const std::string nativeUri = dirUri + "/" + nativeFile;

            auto bytes = assetService->loadBinaryAssetSync(nativeUri);
            if (!bytes)
            {
                VULTRA_CORE_ERROR(
                    "[PluginSystem] '{}': cannot read native library '{}' from package.", manifest.id, nativeUri);
                return false;
            }

            const auto      outPath = pluginExtractionDir(ctx().config.writableRoot) / manifest.id / nativeFile;
            std::error_code ec;
            std::filesystem::create_directories(outPath.parent_path(), ec);
            {
                std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
                if (!out)
                {
                    VULTRA_CORE_ERROR("[PluginSystem] '{}': cannot write extracted native library '{}'.",
                                      manifest.id, outPath.generic_string());
                    return false;
                }
                out.write(reinterpret_cast<const char*>(bytes.value().data()),
                          static_cast<std::streamsize>(bytes.value().size()));
            }

            if (ctx().pluginManager == nullptr || !ctx().pluginManager->load(outPath.generic_string(), ctx()))
            {
                VULTRA_CORE_ERROR("[PluginSystem] '{}': failed to load extracted native library '{}'.",
                                  manifest.id, outPath.generic_string());
                return false;
            }
            VULTRA_CORE_INFO("[PluginSystem] '{}': native library loaded (extracted {}).", manifest.id, nativeFile);
        }

        // --- Lua entry script, read from the asset VFS ------------------------------------------
        if (!manifest.entry.empty())
        {
            const std::string entryUri = dirUri + "/" + manifest.entry;
            auto              text     = assetService->loadTextAssetSync(entryUri);
            if (!text)
            {
                VULTRA_CORE_ERROR(
                    "[PluginSystem] '{}': cannot read Lua entry '{}' from package.", manifest.id, entryUri);
                return false;
            }
            if (!installLuaEntry(manifest, text.value(), entryUri))
                return false;
            VULTRA_CORE_INFO("[PluginSystem] '{}': Lua entry loaded ({}).", manifest.id, manifest.entry);
        }

        m_LoadedIds.push_back(manifest.id);
        VULTRA_CORE_INFO("[PluginSystem] Plugin '{}' ({}) installed.", manifest.name, manifest.id);
        return true;
    }
} // namespace vultra
