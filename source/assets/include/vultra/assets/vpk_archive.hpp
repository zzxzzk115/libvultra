#pragma once

#include <vultra/core/base/error.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
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
        // Require a canonical relative entry name; return an owned copy without accessing the filesystem.
        [[nodiscard]] static std::expected<std::string, Error> canonicalPath(std::string_view path);

        [[nodiscard]] static std::expected<VpkArchive, Error> create(const std::filesystem::path& file);
        // Borrow immutable embedded bytes. They must outlive this archive and every copy/source using it.
        // Identity is a virtual absolute root for asset resolution; it need not exist on disk.
        [[nodiscard]] static std::expected<VpkArchive, Error> create(const std::filesystem::path& identity,
                                                                     std::span<const std::byte>   bytes);

        // Physical archive path, or the virtual identity supplied with embedded bytes.
        const std::filesystem::path& file() const;

        [[nodiscard]] std::expected<bool, Error>                   contains(std::string_view path) const;
        [[nodiscard]] std::expected<uint64_t, Error>               size(std::string_view path) const;
        [[nodiscard]] std::expected<std::vector<std::byte>, Error> read(std::string_view path) const;
        [[nodiscard]] std::expected<void, Error> extractTo(const std::filesystem::path& directory) const;

        // Packs explicit files without interpreting their contents. Output must be a new path.
        [[nodiscard]] static std::expected<void, Error> pack(std::span<const VpkFile>     files,
                                                             const std::filesystem::path& output);
        [[nodiscard]] static std::expected<void, Error> embedProject(const std::filesystem::path& executable,
                                                                     const std::filesystem::path& projectPack,
                                                                     const std::filesystem::path& output);
        [[nodiscard]] static std::expected<std::optional<VpkArchive>, Error>
        embeddedProject(const std::filesystem::path& executable);

    private:
        struct Entry
        {
            std::string path;
            uint64_t    offset;
            uint64_t    size;
            uint64_t    hash;
        };

        // Takes an absolute, normalized file or identity; create() loads the index.
        VpkArchive(std::filesystem::path file, std::span<const std::byte> bytes);
        [[nodiscard]] std::expected<void, Error>         load(uint64_t offset, uint64_t length);
        [[nodiscard]] std::expected<const Entry*, Error> entry(std::string_view path) const;
        std::filesystem::path                            m_File;
        std::span<const std::byte>                       m_Bytes;
        uint64_t                                         m_Offset = 0;
        std::vector<Entry>                               m_Entries;
    };
} // namespace vultra
