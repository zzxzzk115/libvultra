#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vultra
{
    struct VpkFile
    {
        std::string           path; // Canonical relative UTF-8 entry name.
        std::filesystem::path source;
    };

    // Versioned, uncompressed project package. Paths are project-relative UTF-8.
    class VpkArchive
    {
    public:
        explicit VpkArchive(std::filesystem::path file);

        const std::filesystem::path& file() const;

        bool                   contains(std::string_view path) const;
        uint64_t               size(std::string_view path) const;
        std::vector<std::byte> read(std::string_view path) const;
        void                   extractTo(const std::filesystem::path& directory) const;

        // Packs explicit files without interpreting their contents. Output must be a new path.
        static void                      pack(std::span<const VpkFile> files, const std::filesystem::path& output);
        static void                      embedProject(const std::filesystem::path& executable,
                                                      const std::filesystem::path& projectPack,
                                                      const std::filesystem::path& output);
        static std::optional<VpkArchive> embeddedProject(const std::filesystem::path& executable);

    private:
        struct Entry
        {
            std::string path;
            uint64_t    offset;
            uint64_t    size;
            uint64_t    hash;
        };

        VpkArchive(std::filesystem::path file, uint64_t offset, uint64_t length);
        void                  load(uint64_t offset, uint64_t length);
        const Entry&          entry(std::string_view path) const;
        std::filesystem::path m_File;
        uint64_t              m_Offset = 0;
        std::vector<Entry>    m_Entries;
    };
} // namespace vultra
