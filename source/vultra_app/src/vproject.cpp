#include "vproject.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>

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

        fs::path        normalizedPath = path.extension() == ".vproject" ? path : vprojectFileFor(path, path.filename().generic_string());
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

        std::ifstream  file(normalizedPath);
        if (!file)
            return std::nullopt;

        VProject project;
        project.projectDir = fs::absolute(normalizedPath).lexically_normal().parent_path();
        project.name       = project.projectDir.filename().generic_string();

        std::string line;
        while (std::getline(file, line))
        {
            line = trim(std::move(line));
            if (line.empty() || line.front() == '#' || line.front() == '[')
                continue;

            const auto equalsPos = line.find('=');
            if (equalsPos == std::string::npos)
                continue;

            applyKeyValue(project, line.substr(0, equalsPos), line.substr(equalsPos + 1));
        }

        if (project.name.empty())
            project.name = project.projectDir.filename().generic_string();
        if (project.assetRoot.empty())
            project.assetRoot = "resources";
        if (project.defaultScene.empty())
            project.defaultScene = "res://scenes/main.vscn";

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
        return true;
    }
} // namespace vultra_app
