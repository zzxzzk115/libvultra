#pragma once

#include <vultra/core/base/error.hpp>

#include <expected>
#include <filesystem>
#include <optional>
#include <string>

namespace vultra
{
    // Owns extracted files for the executable's embedded engine shader pack. Moving transfers the files.
    class PackagedResources
    {
    public:
        [[nodiscard]] static std::expected<PackagedResources, Error> create();
        ~PackagedResources();
        PackagedResources(PackagedResources&& other) noexcept;
        PackagedResources& operator=(PackagedResources&& other) noexcept;
        PackagedResources(const PackagedResources&)            = delete;
        PackagedResources& operator=(const PackagedResources&) = delete;

        const std::filesystem::path& engineRoot() const;
        const std::string&           shaderHash() const;

    private:
        PackagedResources(std::filesystem::path root, std::string shaderHash);
        void removeFiles();

        std::filesystem::path m_Root;
        std::filesystem::path m_EngineRoot;
        std::string           m_ShaderHash;
    };

    // Existing shader paths are relative to the extracted engine root. Keep this scope outside GPU objects.
    // Moving transfers the duty to restore the previous working directory.
    class ScopedWorkingDirectory
    {
    public:
        [[nodiscard]] static std::expected<ScopedWorkingDirectory, Error> enter(const std::filesystem::path& path);
        ~ScopedWorkingDirectory();
        ScopedWorkingDirectory(ScopedWorkingDirectory&& other) noexcept;
        ScopedWorkingDirectory& operator=(ScopedWorkingDirectory&&)      = delete;
        ScopedWorkingDirectory(const ScopedWorkingDirectory&)            = delete;
        ScopedWorkingDirectory& operator=(const ScopedWorkingDirectory&) = delete;

    private:
        explicit ScopedWorkingDirectory(std::filesystem::path previous);

        std::optional<std::filesystem::path> m_Previous;
    };
} // namespace vultra
