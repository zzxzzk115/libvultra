#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vultra
{
    // File name of a plugin's manifest, placed in the plugin's own directory.
    inline constexpr const char* kPluginManifestFile = "vultra.plugin.vmanifest";

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
    //     "native": "hello",              // optional native library (extension appended)
    //     "entry": "init.lua"             // optional Lua entry script
    //   }
    struct PluginManifest
    {
        std::string              id;
        std::string              name;
        std::string              version;
        std::string              author;
        std::string              description;
        std::string              readme;     // relative path to a README file (optional)
        std::string              repository; // URL (optional)
        std::vector<std::string> platforms;  // empty = all platforms
        std::string              native;     // relative native library name (optional)
        std::string              entry;      // relative Lua entry script (optional)

        std::filesystem::path directory;    // the plugin's folder
        std::filesystem::path manifestPath; // full path to vultra.plugin.vmanifest

        [[nodiscard]] bool supportsPlatform(std::string_view platform) const;
        [[nodiscard]] bool supportsCurrentPlatform() const;
    };

    // The platform token for the running build: "windows" | "linux" | "macos" | "wasm" | "android".
    [[nodiscard]] std::string_view currentPluginPlatform();

    // Parse a single vultra.plugin.vmanifest JSON file. Returns nullopt on error (sets *error).
    [[nodiscard]] std::optional<PluginManifest> loadPluginManifest(const std::filesystem::path& manifestPath,
                                                                   std::string*                 error = nullptr);

    // Scan a directory for `<sub>/vultra.plugin.vmanifest` and parse each one (errors are skipped).
    // A missing directory yields an empty list.
    [[nodiscard]] std::vector<PluginManifest> discoverPlugins(const std::filesystem::path& dir);
} // namespace vultra
