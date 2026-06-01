#include "vproject.hpp"

#include <vasset/vpk.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
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
        }

        void applyKeyValue(VPackageManifest& manifest, std::string key, std::string value)
        {
            key   = trim(std::move(key));
            value = unquote(std::move(value));

            if (key == "name")
                manifest.name = value;
            else if (key == "entry_scene")
                manifest.entryScene = value;
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
