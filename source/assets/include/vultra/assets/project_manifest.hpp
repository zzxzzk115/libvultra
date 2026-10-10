#pragma once

#include <vultra/assets/fbx_import.hpp>
#include <vultra/core/base/stable_id.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace vultra
{
    class AssetSource;

    struct AssetId
    {
        StableId    value;
        friend bool operator==(const AssetId&, const AssetId&) = default;
    };

    struct ProjectAsset
    {
        AssetId                         id;
        std::filesystem::path           path; // Relative to the project file's directory.
        std::optional<FbxImportOptions> fbx;
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

    struct ResearchMethod
    {
        std::string name;
        AssetId     graph;
    };

    struct ResearchProject
    {
        std::string                 name;
        uint32_t                    width    = 640;
        uint32_t                    height   = 480;
        uint64_t                    features = 0;
        std::vector<ResearchMethod> methods;
        AssetId                     comparison;
        std::string                 renderPath = "forward";
        std::string                 referenceMethod;
        std::string                 configurationLabel = "Configuration";
        std::string                 rendererSettings = "{}"; // Reflected RenderSettings JSON; interpreted by the host.
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
        std::optional<ResearchProject>     research;

        AssetId addAsset(const std::filesystem::path& path, std::optional<FbxImportOptions> fbx = std::nullopt);
        void    renameAsset(AssetId id, const std::filesystem::path& path);
        const ProjectAsset&              asset(AssetId id) const;
        const std::vector<ProjectAsset>& assets() const;

        // Retain the source until script hosts stop; native/.NET loaders borrow these real files.
        std::filesystem::path materializeModule(const AssetSource& source, const std::filesystem::path& module) const;

        void                   save(const std::filesystem::path& file) const;
        static ProjectManifest load(const std::filesystem::path& file, const AssetSource* source = nullptr);

    private:
        void                      validate() const;
        std::vector<ProjectAsset> m_Assets;
    };
} // namespace vultra
