#include "vproject.hpp"

#include <vasset/vpk.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace vultra_app
{
    namespace
    {
        std::string trim(std::string value)
        {
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
                value.erase(value.begin());
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
                value.pop_back();
            return value;
        }

        std::string unquote(std::string value)
        {
            value = trim(std::move(value));
            if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
                return value.substr(1, value.size() - 2);
            return value;
        }

        bool parseBool(std::string value, const bool fallback = true)
        {
            value = trim(std::move(value));
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            if (value == "1" || value == "true" || value == "yes" || value == "on")
                return true;
            if (value == "0" || value == "false" || value == "no" || value == "off")
                return false;
            return fallback;
        }

        std::optional<uint32_t> parseIndexedKey(std::string_view key, std::string_view prefix)
        {
            if (!key.starts_with(prefix))
                return std::nullopt;
            const auto suffix = key.substr(prefix.size());
            if (suffix.empty())
                return std::nullopt;
            uint32_t index = 0;
            for (const char ch : suffix)
            {
                if (!std::isdigit(static_cast<unsigned char>(ch)))
                    return std::nullopt;
                index = index * 10u + static_cast<uint32_t>(ch - '0');
            }
            return index;
        }

        VBuildScene& ensureBuildScene(std::vector<VBuildScene>& scenes, const uint32_t index)
        {
            auto it = std::find_if(scenes.begin(), scenes.end(), [&](const VBuildScene& scene) {
                return scene.index == index;
            });
            if (it != scenes.end())
                return *it;

            scenes.push_back(VBuildScene {.index = index});
            return scenes.back();
        }

        void applyKeyValue(VProject& project, std::string key, std::string value)
        {
            key   = trim(std::move(key));
            value = unquote(std::move(value));

            if (key == "name")
                project.name = value;
            else if (key == "asset_root")
                project.assetRoot = value;
            else if (key == "default_scene")
                project.defaultScene = value;
            else if (key == "editing_rendergraph")
                project.editingRenderGraph = value;
            else if (auto index = parseIndexedKey(key, "build_scene."))
                ensureBuildScene(project.buildScenes, *index).uri = value;
            else if (auto index = parseIndexedKey(key, "build_scene_alias."))
                ensureBuildScene(project.buildScenes, *index).alias = value;
            // Legacy: the old free-form per-scene "name" is migrated into the alias.
            else if (auto index = parseIndexedKey(key, "build_scene_name."))
                ensureBuildScene(project.buildScenes, *index).alias = value;
            else if (auto index = parseIndexedKey(key, "build_scene_enabled."))
                ensureBuildScene(project.buildScenes, *index).enabled = parseBool(value);
        }

        void applyKeyValue(VPackageManifest& manifest, std::string key, std::string value)
        {
            key   = trim(std::move(key));
            value = unquote(std::move(value));

            if (key == "name")
                manifest.name = value;
            else if (key == "entry_scene")
                manifest.entryScene = value;
            else if (auto index = parseIndexedKey(key, "build_scene."))
                ensureBuildScene(manifest.buildScenes, *index).uri = value;
            else if (auto index = parseIndexedKey(key, "build_scene_alias."))
                ensureBuildScene(manifest.buildScenes, *index).alias = value;
            // Legacy: the old free-form per-scene "name" is migrated into the alias.
            else if (auto index = parseIndexedKey(key, "build_scene_name."))
                ensureBuildScene(manifest.buildScenes, *index).alias = value;
            else if (auto index = parseIndexedKey(key, "build_scene_enabled."))
                ensureBuildScene(manifest.buildScenes, *index).enabled = parseBool(value);
        }

        std::string quote(std::string_view value)
        {
            std::string out = "\"";
            for (const char ch : value)
            {
                if (ch == '"')
                    out += "\\\"";
                else
                    out += ch;
            }
            out += "\"";
            return out;
        }

        std::filesystem::path resolveProjectAssetUri(const VProject& project, const std::string_view uri)
        {
            constexpr std::string_view prefix {"res://"};
            if (!uri.starts_with(prefix))
                return {};
            return (project.projectDir / project.assetRoot / std::string(uri.substr(prefix.size()))).lexically_normal();
        }

        bool projectAssetUriExists(const VProject& project, const std::string_view uri)
        {
            const auto path = resolveProjectAssetUri(project, uri);
            if (path.empty())
                return false;
            std::error_code ec;
            return std::filesystem::is_regular_file(path, ec);
        }

        std::optional<std::string> readPackageEntryScene(const VProject& project)
        {
            const auto manifestPath = project.projectDir / project.assetRoot / kVPackageManifestPath;
            std::ifstream file(manifestPath);
            if (!file)
                return std::nullopt;

            std::string line;
            while (std::getline(file, line))
            {
                line = trim(std::move(line));
                if (line.empty() || line.front() == '#' || line.front() == '[')
                    continue;

                const auto equalsPos = line.find('=');
                if (equalsPos == std::string::npos)
                    continue;

                auto key = trim(line.substr(0, equalsPos));
                if (key != "entry_scene")
                    continue;

                auto value = unquote(line.substr(equalsPos + 1));
                if (!value.empty())
                    return value;
            }
            return std::nullopt;
        }

        std::string findFallbackSceneUri(const VProject& project)
        {
            if (auto entryScene = readPackageEntryScene(project);
                entryScene.has_value() && projectAssetUriExists(project, *entryScene))
            {
                return *entryScene;
            }

            const auto scenesDir = project.projectDir / project.assetRoot / "scenes";
            std::error_code ec;
            if (!std::filesystem::is_directory(scenesDir, ec))
                return {};

            std::vector<std::filesystem::path> sceneFiles;
            for (const auto& entry : std::filesystem::directory_iterator(scenesDir, ec))
            {
                if (entry.is_regular_file(ec) && entry.path().extension() == ".vscn")
                    sceneFiles.push_back(entry.path().lexically_normal());
            }
            std::sort(sceneFiles.begin(), sceneFiles.end());
            if (sceneFiles.empty())
                return {};

            auto rel = std::filesystem::relative(sceneFiles.front(), project.projectDir / project.assetRoot, ec);
            if (ec)
                return {};
            return "res://" + rel.generic_string();
        }
    } // namespace

    std::string buildSceneName(std::string_view uri)
    {
        if (uri.empty())
            return {};
        return std::filesystem::path(uri).stem().generic_string();
    }

    const VBuildScene* findBuildScene(const std::vector<VBuildScene>& scenes, std::string_view nameOrAlias)
    {
        if (nameOrAlias.empty())
            return nullptr;
        for (const auto& scene : scenes)
        {
            if (scene.alias == nameOrAlias || buildSceneName(scene.uri) == nameOrAlias)
                return &scene;
        }
        return nullptr;
    }

    std::vector<VBuildScene> normalizedBuildScenes(const std::string&              defaultScene,
                                                   const std::vector<VBuildScene>& scenes)
    {
        std::vector<VBuildScene> out;
        for (const auto& scene : scenes)
        {
            if (scene.uri.empty())
                continue;
            out.push_back(scene);
        }

        if (!defaultScene.empty())
        {
            const auto hasDefault = std::any_of(out.begin(), out.end(), [&](const VBuildScene& scene) {
                return scene.uri == defaultScene;
            });
            if (!hasDefault)
                out.push_back(VBuildScene {.index = 0, .uri = defaultScene, .enabled = true});
        }

        std::sort(out.begin(), out.end(), [](const VBuildScene& a, const VBuildScene& b) {
            if (a.index != b.index)
                return a.index < b.index;
            return a.uri < b.uri;
        });

        for (uint32_t i = 0; i < static_cast<uint32_t>(out.size()); ++i)
            out[i].index = i;
        return out;
    }

    std::filesystem::path vprojectFileFor(const std::filesystem::path& projectDir, const std::string& projectName)
    {
        const std::string filename = projectName.empty() ? projectDir.filename().generic_string() : projectName;
        return projectDir / (filename + ".vproject");
    }

    std::optional<VProject> loadVProject(const std::filesystem::path& path)
    {
        namespace fs = std::filesystem;

        fs::path normalizedPath =
            path.extension() == ".vproject" ? path : vprojectFileFor(path, path.filename().generic_string());
        std::error_code ec;
        if (!fs::exists(normalizedPath, ec) && path.extension() != ".vproject" && fs::is_directory(path, ec))
        {
            for (const auto& entry : fs::directory_iterator(path, ec))
            {
                if (entry.is_regular_file(ec) && entry.path().extension() == ".vproject")
                {
                    normalizedPath = entry.path();
                    break;
                }
            }
        }

        std::ifstream file(normalizedPath);
        if (!file)
            return std::nullopt;

        VProject project;
        project.projectDir = fs::absolute(normalizedPath).lexically_normal().parent_path();
        project.name       = project.projectDir.filename().generic_string();

        bool        hasEditingRenderGraph = false;
        std::string line;
        while (std::getline(file, line))
        {
            line = trim(std::move(line));
            if (line.empty() || line.front() == '#' || line.front() == '[')
                continue;

            const auto equalsPos = line.find('=');
            if (equalsPos == std::string::npos)
                continue;

            if (trim(line.substr(0, equalsPos)) == "editing_rendergraph")
                hasEditingRenderGraph = true;
            applyKeyValue(project, line.substr(0, equalsPos), line.substr(equalsPos + 1));
        }

        if (project.name.empty())
            project.name = project.projectDir.filename().generic_string();
        if (project.assetRoot.empty())
            project.assetRoot = "resources";
        if (project.defaultScene.empty() || !projectAssetUriExists(project, project.defaultScene))
            project.defaultScene = findFallbackSceneUri(project);
        project.buildScenes = normalizedBuildScenes(project.defaultScene, project.buildScenes);
        if (!hasEditingRenderGraph && project.editingRenderGraph.empty())
            project.editingRenderGraph = "res://render/default.vrg.json";

        return project;
    }

    bool saveVProject(const VProject& project, std::string* errorMessage)
    {
        namespace fs = std::filesystem;

        std::error_code ec;
        fs::create_directories(project.projectDir, ec);
        if (ec)
        {
            if (errorMessage != nullptr)
                *errorMessage = ec.message();
            return false;
        }

        std::ofstream file(vprojectFileFor(project.projectDir, project.name), std::ios::trunc);
        if (!file)
        {
            if (errorMessage != nullptr)
                *errorMessage = "failed to open .vproject for writing";
            return false;
        }

        file << "[vproject]\n";
        file << "version = 1\n";
        file << "name = \"" << project.name << "\"\n";
        file << "asset_root = \"" << project.assetRoot << "\"\n";
        file << "default_scene = \"" << project.defaultScene << "\"\n";
        file << "editing_rendergraph = \"" << project.editingRenderGraph << "\"\n";
        const auto buildScenes = normalizedBuildScenes(project.defaultScene, project.buildScenes);
        for (const auto& scene : buildScenes)
        {
            file << "build_scene." << scene.index << " = " << quote(scene.uri) << "\n";
            if (!scene.alias.empty())
                file << "build_scene_alias." << scene.index << " = " << quote(scene.alias) << "\n";
            file << "build_scene_enabled." << scene.index << " = " << (scene.enabled ? "true" : "false") << "\n";
        }
        return true;
    }

    bool saveVPackageManifest(const std::filesystem::path& assetRoot,
                              const VPackageManifest&      manifest,
                              std::string*                 errorMessage)
    {
        namespace fs = std::filesystem;

        std::error_code ec;
        fs::create_directories(assetRoot, ec);
        if (ec)
        {
            if (errorMessage != nullptr)
                *errorMessage = ec.message();
            return false;
        }

        const auto    manifestPath = assetRoot / kVPackageManifestPath;
        std::ofstream file(manifestPath, std::ios::trunc);
        if (!file)
        {
            if (errorMessage != nullptr)
                *errorMessage = "failed to open package manifest for writing: " + manifestPath.generic_string();
            return false;
        }

        file << "[vpackage]\n";
        file << "version = 1\n";
        file << "name = " << quote(manifest.name) << "\n";
        file << "entry_scene = " << quote(manifest.entryScene) << "\n";
        const auto buildScenes = normalizedBuildScenes(manifest.entryScene, manifest.buildScenes);
        for (const auto& scene : buildScenes)
        {
            file << "build_scene." << scene.index << " = " << quote(scene.uri) << "\n";
            if (!scene.alias.empty())
                file << "build_scene_alias." << scene.index << " = " << quote(scene.alias) << "\n";
            file << "build_scene_enabled." << scene.index << " = " << (scene.enabled ? "true" : "false") << "\n";
        }
        return true;
    }

    std::optional<VPackageManifest> loadVPackageManifestText(const std::string& text)
    {
        VPackageManifest manifest;

        size_t begin = 0;
        while (begin <= text.size())
        {
            size_t end = text.find('\n', begin);
            if (end == std::string::npos)
                end = text.size();

            std::string line = trim(text.substr(begin, end - begin));
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (!line.empty() && line.front() != '#' && line.front() != '[')
            {
                const auto equalsPos = line.find('=');
                if (equalsPos != std::string::npos)
                    applyKeyValue(manifest, line.substr(0, equalsPos), line.substr(equalsPos + 1));
            }

            if (end == text.size())
                break;
            begin = end + 1;
        }

        return manifest;
    }

    std::optional<VPackageManifest> loadVPackageManifestFromVpk(const std::filesystem::path& vpkPath,
                                                                std::string*                 errorMessage)
    {
        const auto opened = vasset::openVpk(vpkPath.generic_string());
        if (!opened)
        {
            if (errorMessage != nullptr)
                *errorMessage = "failed to open VPK";
            return std::nullopt;
        }

        auto bytes = vasset::readVpkFile(opened.value(), vpkPath.generic_string(), kVPackageManifestPath);
        if (!bytes)
            bytes = vasset::readVpkFile(opened.value(), vpkPath.generic_string(), kVPackageManifestUri);
        if (!bytes)
        {
            if (errorMessage != nullptr)
                *errorMessage = "package manifest not found in VPK";
            return std::nullopt;
        }

        std::string text;
        text.resize(bytes.value().size());
        std::transform(
            bytes.value().begin(), bytes.value().end(), text.begin(), [](std::byte b) { return static_cast<char>(b); });
        return loadVPackageManifestText(text);
    }
} // namespace vultra_app
