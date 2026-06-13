#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vultra
{
    struct EngineContext;

    // File name of a plugin's manifest, placed in the plugin's own directory.
    inline constexpr const char* kPluginManifestFile = "vultra.plugin.vmanifest";

    enum class PluginLoadPhase
    {
        eNormal,
        ePreRenderDevice,
    };

    enum class PluginConfigParamType
    {
        eString,
        ePath,
        eBool,
        eInt,
        eFloat,
        eEnum, // one of `options`; drawn as a dropdown, stored/exported as the option string
    };

    struct PluginConfigParam
    {
        std::string              key;
        std::string              label;
        std::string              description;
        PluginConfigParamType    type {PluginConfigParamType::eString};
        std::string              defaultValue;
        std::string              envVar;
        std::vector<std::string> options; // eEnum: the allowed values
        bool                     required {false};
        bool                     secret {false};
    };

    // Describes a plugin discovered from a vultra.plugin.vmanifest (a JSON document). Example:
    //
    //   {
    //     "schemaVersion": 1,
    //     "id": "com.example.hello",     // required, unique
    //     "name": "Hello",
    //     "version": "1.0.0",
    //     "author": "Jane Doe",
    //     "description": "A friendly greeter.",
    //     "readme": "README.md",          // optional, relative to the plugin dir
    //     "repository": "https://github.com/example/hello",
    //     "platforms": ["windows", "linux", "macos"],   // empty/absent = all platforms
    //     "loadPhase": "pre_render_device", // optional; native-only early phase
    //     "native": "hello",              // optional native library (extension appended)
    //     "entry": "init.lua",            // optional Lua entry script
    //     "editorOnly": true,             // optional; whole plugin is editor-only, excluded from exported VPKs
    //     "editorOnlyFiles": ["editor/**", "panels/*.lua"] // optional; per-file editor-only globs (mixed plugins)
    //   }
    struct PluginManifest
    {
        std::string              id;
        std::string              name;
        std::string              version;
        // Lowest engine version this plugin supports, as a dotted semantic string (manifest
        // "minEngineVersion"). Empty = no declared minimum (treated as compatible). The editor uses
        // this to gate install/enable against the running engine version.
        std::string              minEngineVersion;
        std::string              author;
        std::string              description;
        std::string              readme;     // relative path to a README file (optional)
        std::string              repository; // URL (optional)
        std::vector<std::string> platforms;  // empty = all platforms
        PluginLoadPhase          loadPhase {PluginLoadPhase::eNormal};
        bool                     restartRequired {false}; // manifest "restartRequired": true
        std::string              native;     // relative native library name (optional)
        std::string              entry;      // relative Lua entry script (optional)
        // Ids of other plugins that must load before this one (manifest
        // "dependencies": ["com.x.y", ...]). PluginSystem topologically sorts
        // the enabled set by this; a missing/disabled dependency fails only
        // this plugin, not the whole boot. (Version-range matching is future
        // work; ids are matched exactly today.)
        std::vector<std::string>       dependencies;
        std::vector<PluginConfigParam> configParams;

        // Editor-only content. `editorOnly` marks the whole plugin as editor-only: the editor still
        // discovers, enables and loads it, but the VPK exporter skips it entirely (it can never run
        // in a runtime build, where the `Editor` Lua global is nil). `editorOnlyFiles` lists
        // plugin-relative globs (forward slashes) for mixed plugins: those files are dropped from
        // the exported VPK while the rest of the plugin ships. Authors must guard editor-only code
        // with `if Editor then ... end` so the runtime never requires an excluded file.
        bool                     editorOnly {false};
        std::vector<std::string> editorOnlyFiles;

        std::filesystem::path directory;    // the plugin's folder
        std::filesystem::path manifestPath; // full path to vultra.plugin.vmanifest

        [[nodiscard]] bool supportsPlatform(std::string_view platform) const;
        [[nodiscard]] bool supportsCurrentPlatform() const;

        // Whether enabling/disabling this plugin only takes effect on the next launch. True when
        // the manifest says so explicitly, and implicitly for pre-render-device plugins (their
        // native library must install hooks before the render device exists).
        [[nodiscard]] bool needsRestartToApply() const
        {
            return restartRequired || loadPhase == PluginLoadPhase::ePreRenderDevice;
        }
    };

    // The platform token for the running build: "windows" | "linux" | "macos" | "wasm" | "android".
    [[nodiscard]] std::string_view currentPluginPlatform();

    // Parse a single vultra.plugin.vmanifest JSON file. Returns nullopt on error (sets *error).
    [[nodiscard]] std::optional<PluginManifest> loadPluginManifest(const std::filesystem::path& manifestPath,
                                                                   std::string*                 error = nullptr);

    // Scan a directory for `<sub>/vultra.plugin.vmanifest` and parse each one (errors are skipped).
    // A missing directory yields an empty list.
    [[nodiscard]] std::vector<PluginManifest> discoverPlugins(const std::filesystem::path& dir);

    // Load only native libraries for plugins with loadPhase == "pre_render_device".
    // Intended to run before RenderBackendSystem creates the Vulkan instance/device.
    bool loadPreRenderDeviceNativePlugins(EngineContext& ctx);
} // namespace vultra
