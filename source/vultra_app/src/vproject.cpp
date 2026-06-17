#include "vproject.hpp"

#include <vasset/vpk.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
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

        bool setProcessEnv(const std::string& key, const std::string& value)
        {
#ifdef _WIN32
            return _putenv_s(key.c_str(), value.c_str()) == 0;
#else
            return setenv(key.c_str(), value.c_str(), 1) == 0;
#endif
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
            else if (key.starts_with("export."))
            {
                // export.<platform>.<setting>: platform ids and setting keys have no dots, so split on
                // the first dot after the "export." prefix.
                const std::string_view rest {key.data() + std::string_view {"export."}.size(),
                                             key.size() - std::string_view {"export."}.size()};
                const auto             dot = rest.find('.');
                if (dot != std::string_view::npos && dot > 0 && dot + 1 < rest.size())
                {
                    const auto platform   = std::string(rest.substr(0, dot));
                    const auto settingKey = std::string(rest.substr(dot + 1));
                    project.exportSettings[platform][settingKey] = value;
                }
            }
            else if (key == "enabled_plugins")
            {
                project.enabledPlugins.clear();
                std::string item;
                std::istringstream stream(value);
                while (std::getline(stream, item, ','))
                {
                    item = trim(std::move(item));
                    if (!item.empty())
                        project.enabledPlugins.push_back(item);
                }
            }
            else if (key.starts_with("plugin_config."))
            {
                const std::string_view rest {key.data() + std::string_view {"plugin_config."}.size(),
                                             key.size() - std::string_view {"plugin_config."}.size()};
                const auto             dot = rest.find_last_of('.');
                if (dot != std::string_view::npos && dot > 0 && dot + 1 < rest.size())
                {
                    const auto pluginId = std::string(rest.substr(0, dot));
                    const auto paramKey = std::string(rest.substr(dot + 1));
                    project.pluginConfigValues[pluginId][paramKey] = value;
                }
            }
            else if (auto index = parseIndexedKey(key, "build_scene."))
                ensureBuildScene(project.buildScenes, *index).uri = value;
            else if (auto index = parseIndexedKey(key, "build_scene_alias."))
                ensureBuildScene(project.buildScenes, *index).alias = value;
            // Legacy: the old free-form per-scene "name" is migrated into the alias.
            else if (auto index = parseIndexedKey(key, "build_scene_name."))
                ensureBuildScene(project.buildScenes, *index).alias = value;
            else if (auto index = parseIndexedKey(key, "build_scene_enabled."))
                ensureBuildScene(project.buildScenes, *index).enabled = parseBool(value);
            else if (key == "tags")
            {
                project.tags.clear();
                std::string        item;
                std::istringstream stream(value);
                while (std::getline(stream, item, ','))
                {
                    item = trim(std::move(item));
                    if (!item.empty())
                        project.tags.push_back(item);
                }
            }
            else if (auto index = parseIndexedKey(key, "layer."))
            {
                if (*index < project.layerNames.size())
                    project.layerNames[*index] = value;
            }
            else if (auto index = parseIndexedKey(key, "physics_layer."))
            {
                if (*index < project.physicsLayerNames.size())
                    project.physicsLayerNames[*index] = value;
            }
            else if (key == "physics_no_collide")
            {
                // Comma-separated "a:b" pairs whose collision is disabled.
                project.physicsCollisionDisabled.clear();
                std::string        item;
                std::istringstream stream(value);
                while (std::getline(stream, item, ','))
                {
                    item             = trim(std::move(item));
                    const auto colon = item.find(':');
                    if (colon == std::string::npos)
                        continue;
                    const auto a = static_cast<uint32_t>(std::stoul(item.substr(0, colon)));
                    const auto b = static_cast<uint32_t>(std::stoul(item.substr(colon + 1)));
                    project.physicsCollisionDisabled.push_back({a, b});
                }
            }
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
            else if (key == "plugin_dirs")
            {
                manifest.pluginDirs.clear();
                std::istringstream stream(value);
                std::string        item;
                while (std::getline(stream, item, ','))
                {
                    item = trim(std::move(item));
                    if (!item.empty())
                        manifest.pluginDirs.push_back(item);
                }
            }
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

        std::string quoteEnvValue(std::string_view value)
        {
            const bool needsQuote = value.empty() || value.find_first_of(" \t#") != std::string_view::npos;
            if (!needsQuote)
                return std::string {value};
            return quote(value);
        }

        std::optional<std::string> parseEnvLineKey(std::string line)
        {
            line = trim(std::move(line));
            if (line.empty() || line.front() == '#')
                return std::nullopt;
            if (line.starts_with("export "))
                line = trim(line.substr(7));
            const auto equalsPos = line.find('=');
            if (equalsPos == std::string::npos)
                return std::nullopt;
            auto key = trim(line.substr(0, equalsPos));
            if (key.empty())
                return std::nullopt;
            return key;
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

    std::array<std::string, 32> defaultLayerNames()
    {
        std::array<std::string, 32> layers {};
        layers[0] = "Default";
        layers[5] = "UI";
        return layers;
    }

    std::vector<std::string> defaultTags() { return {"Untagged"}; }

    std::array<std::string, 32> defaultPhysicsLayerNames()
    {
        std::array<std::string, 32> layers {};
        layers[0] = "Default";
        return layers;
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

        // Projects with no tag/layer config (new or pre-feature) get the built-in defaults.
        if (project.tags.empty())
            project.tags = defaultTags();
        if (std::none_of(project.layerNames.begin(), project.layerNames.end(), [](const std::string& n) {
                return !n.empty();
            }))
            project.layerNames = defaultLayerNames();
        if (std::none_of(project.physicsLayerNames.begin(),
                         project.physicsLayerNames.end(),
                         [](const std::string& n) { return !n.empty(); }))
            project.physicsLayerNames = defaultPhysicsLayerNames();

        return project;
    }

    bool loadProjectEnvFile(const std::filesystem::path& projectDir, std::string* errorMessage)
    {
        const auto envPath = (projectDir / ".env").lexically_normal();

        std::error_code ec;
        if (!std::filesystem::exists(envPath, ec))
            return true;

        std::ifstream file(envPath);
        if (!file)
        {
            if (errorMessage != nullptr)
                *errorMessage = "failed to open project .env: " + envPath.generic_string();
            return false;
        }

        std::string line;
        bool        ok = true;
        while (std::getline(file, line))
        {
            line = trim(std::move(line));
            if (line.empty() || line.front() == '#')
                continue;
            if (line.starts_with("export "))
                line = trim(line.substr(7));

            const auto equalsPos = line.find('=');
            if (equalsPos == std::string::npos)
                continue;

            auto key   = trim(line.substr(0, equalsPos));
            auto value = unquote(line.substr(equalsPos + 1));
            if (!key.empty() && !setProcessEnv(key, value))
            {
                ok = false;
                if (errorMessage != nullptr)
                    *errorMessage = "failed to set environment variable from .env: " + key;
            }
        }

        return ok;
    }

    bool saveProjectEnvValues(const std::filesystem::path&                         projectDir,
                              const std::unordered_map<std::string, std::string>& values,
                              std::string*                                        errorMessage)
    {
        const auto envPath = (projectDir / ".env").lexically_normal();

        std::vector<std::string> lines;
        {
            std::ifstream input(envPath);
            std::string   line;
            while (std::getline(input, line))
                lines.push_back(line);
        }

        auto pending = values;
        std::vector<std::string> out;
        out.reserve(lines.size() + pending.size());
        for (const auto& line : lines)
        {
            auto key = parseEnvLineKey(line);
            if (!key.has_value())
            {
                out.push_back(line);
                continue;
            }

            auto it = pending.find(*key);
            if (it == pending.end())
            {
                out.push_back(line);
                continue;
            }

            if (!it->second.empty())
                out.push_back(*key + "=" + quoteEnvValue(it->second));
            pending.erase(it);
        }

        for (const auto& [key, value] : pending)
        {
            if (!key.empty() && !value.empty())
                out.push_back(key + "=" + quoteEnvValue(value));
        }

        std::error_code ec;
        std::filesystem::create_directories(projectDir, ec);
        if (ec)
        {
            if (errorMessage != nullptr)
                *errorMessage = ec.message();
            return false;
        }

        std::ofstream output(envPath, std::ios::trunc);
        if (!output)
        {
            if (errorMessage != nullptr)
                *errorMessage = "failed to open project .env for writing: " + envPath.generic_string();
            return false;
        }

        for (const auto& line : out)
            output << line << "\n";
        return true;
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
        // Per-platform export settings, written deterministically (sorted by platform, then key).
        {
            std::vector<std::string> platforms;
            platforms.reserve(project.exportSettings.size());
            for (const auto& [platform, _] : project.exportSettings)
            {
                static_cast<void>(_);
                platforms.push_back(platform);
            }
            std::sort(platforms.begin(), platforms.end());
            for (const auto& platform : platforms)
            {
                const auto&              settings = project.exportSettings.at(platform);
                std::vector<std::string> keys;
                keys.reserve(settings.size());
                for (const auto& [key, _] : settings)
                {
                    static_cast<void>(_);
                    keys.push_back(key);
                }
                std::sort(keys.begin(), keys.end());
                for (const auto& key : keys)
                {
                    const auto& value = settings.at(key);
                    if (!value.empty())
                        file << "export." << platform << "." << key << " = " << quote(value) << "\n";
                }
            }
        }
        if (!project.enabledPlugins.empty())
        {
            std::string joined;
            for (std::size_t i = 0; i < project.enabledPlugins.size(); ++i)
                joined += (i == 0 ? "" : ",") + project.enabledPlugins[i];
            file << "enabled_plugins = " << quote(joined) << "\n";
        }
        std::vector<std::string> pluginIds;
        pluginIds.reserve(project.pluginConfigValues.size());
        for (const auto& [pluginId, _] : project.pluginConfigValues)
        {
            static_cast<void>(_);
            pluginIds.push_back(pluginId);
        }
        std::sort(pluginIds.begin(), pluginIds.end());
        for (const auto& pluginId : pluginIds)
        {
            const auto& values = project.pluginConfigValues.at(pluginId);
            std::vector<std::string> keys;
            keys.reserve(values.size());
            for (const auto& [key, _] : values)
            {
                static_cast<void>(_);
                keys.push_back(key);
            }
            std::sort(keys.begin(), keys.end());
            for (const auto& key : keys)
            {
                const auto& value = values.at(key);
                if (!value.empty())
                    file << "plugin_config." << pluginId << "." << key << " = " << quote(value) << "\n";
            }
        }
        const auto buildScenes = normalizedBuildScenes(project.defaultScene, project.buildScenes);
        for (const auto& scene : buildScenes)
        {
            file << "build_scene." << scene.index << " = " << quote(scene.uri) << "\n";
            if (!scene.alias.empty())
                file << "build_scene_alias." << scene.index << " = " << quote(scene.alias) << "\n";
            file << "build_scene_enabled." << scene.index << " = " << (scene.enabled ? "true" : "false") << "\n";
        }
        if (!project.tags.empty())
        {
            std::string joined;
            for (size_t i = 0; i < project.tags.size(); ++i)
            {
                if (i != 0)
                    joined += ',';
                joined += project.tags[i];
            }
            file << "tags = " << quote(joined) << "\n";
        }
        for (size_t i = 0; i < project.layerNames.size(); ++i)
        {
            if (!project.layerNames[i].empty())
                file << "layer." << i << " = " << quote(project.layerNames[i]) << "\n";
        }
        for (size_t i = 0; i < project.physicsLayerNames.size(); ++i)
        {
            if (!project.physicsLayerNames[i].empty())
                file << "physics_layer." << i << " = " << quote(project.physicsLayerNames[i]) << "\n";
        }
        if (!project.physicsCollisionDisabled.empty())
        {
            std::string joined;
            for (size_t i = 0; i < project.physicsCollisionDisabled.size(); ++i)
            {
                if (i != 0)
                    joined += ',';
                joined += std::to_string(project.physicsCollisionDisabled[i][0]) + ':' +
                          std::to_string(project.physicsCollisionDisabled[i][1]);
            }
            file << "physics_no_collide = " << quote(joined) << "\n";
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
        if (!manifest.pluginDirs.empty())
        {
            std::string joined;
            for (std::size_t i = 0; i < manifest.pluginDirs.size(); ++i)
                joined += (i == 0 ? "" : ",") + manifest.pluginDirs[i];
            file << "plugin_dirs = " << quote(joined) << "\n";
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
