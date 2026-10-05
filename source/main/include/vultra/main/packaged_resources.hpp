#pragma once

#include <filesystem>
#include <string>

namespace vultra
{
    class VpkArchive;

    // Owns extracted files for the executable's embedded shader pack and optional project pack.
    class PackagedResources
    {
    public:
        PackagedResources();
        ~PackagedResources();
        PackagedResources(const PackagedResources&)            = delete;
        PackagedResources& operator=(const PackagedResources&) = delete;

        const std::filesystem::path& engineRoot() const;
        const std::string&           shaderHash() const;
        std::filesystem::path        extractProject(const VpkArchive& archive);

    private:
        std::filesystem::path m_Root;
        std::filesystem::path m_EngineRoot;
        std::string           m_ShaderHash;
    };

    // Existing shader paths are relative to the extracted engine root. Keep this scope outside GPU objects.
    class ScopedWorkingDirectory
    {
    public:
        explicit ScopedWorkingDirectory(const std::filesystem::path& path);
        ~ScopedWorkingDirectory();
        ScopedWorkingDirectory(const ScopedWorkingDirectory&)            = delete;
        ScopedWorkingDirectory& operator=(const ScopedWorkingDirectory&) = delete;

    private:
        std::filesystem::path m_Previous;
    };
} // namespace vultra
