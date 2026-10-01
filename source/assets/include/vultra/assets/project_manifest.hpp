#pragma once

#include <vultra/core/base/stable_id.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace vultra
{
    struct AssetId
    {
        StableId    value;
        friend bool operator==(const AssetId&, const AssetId&) = default;
    };

    struct ProjectAsset
    {
        AssetId               id;
        std::filesystem::path path; // Relative to the project file's directory.
    };

    struct ScriptModule
    {
        enum class Language
        {
            eNative,
            eLua,
            eCSharp
        };

        Language                language;
        std::filesystem::path   path;
        std::string             typeName; // Fully qualified C# class name.
        std::optional<StableId> node;     // Persistent scene node ID, resolved when the module starts.
    };

    // The project's stable path-to-ID map. Renaming an entry preserves references in scenes.
    class ProjectManifest
    {
    public:
        std::filesystem::path              mainScene;
        std::optional<AssetId>             environment;
        std::optional<AssetId>             uiDocument;
        std::optional<AssetId>             uiFont;
        std::vector<std::filesystem::path> extensions;
        std::vector<ScriptModule>          scripts;

        AssetId                          addAsset(const std::filesystem::path& path);
        void                             renameAsset(AssetId id, const std::filesystem::path& path);
        const ProjectAsset&              asset(AssetId id) const;
        const std::vector<ProjectAsset>& assets() const;

        void                   save(const std::filesystem::path& file) const;
        static ProjectManifest load(const std::filesystem::path& file);

    private:
        void                      validate() const;
        std::vector<ProjectAsset> m_Assets;
    };
} // namespace vultra
