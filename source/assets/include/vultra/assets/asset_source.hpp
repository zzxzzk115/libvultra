#pragma once

#include <vultra/assets/vpk_archive.hpp>
#include <vultra/core/base/error.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace vultra
{
    // Explicit filesystem or VPK source. Resolved package paths are identities, not extracted files.
    // Reads own their bytes and may run concurrently; materialization runs on the owning thread.
    class AssetSource
    {
    public:
        // The root must be an existing directory. The source is not movable: materialized paths refer to it.
        [[nodiscard]] static std::expected<std::unique_ptr<AssetSource>, Error>
        create(const std::filesystem::path& root);
        explicit AssetSource(VpkArchive archive);
        ~AssetSource();
        AssetSource(const AssetSource&)            = delete;
        AssetSource& operator=(const AssetSource&) = delete;

        const std::filesystem::path&                               root() const;
        [[nodiscard]] std::expected<std::filesystem::path, Error>  resolve(const std::filesystem::path& path) const;
        [[nodiscard]] std::expected<bool, Error>                   contains(const std::filesystem::path& path) const;
        [[nodiscard]] std::expected<uint64_t, Error>               size(const std::filesystem::path& path) const;
        [[nodiscard]] std::expected<std::vector<std::byte>, Error> read(const std::filesystem::path& path) const;
        // For loaders that require real files, such as native modules and .NET assemblies.
        // Returned paths expire with this source. This never extracts unrelated entries.
        [[nodiscard]] std::expected<std::filesystem::path, Error> materialize(const std::filesystem::path& path) const;

    private:
        // Takes the canonical directory checked by create().
        explicit AssetSource(std::filesystem::path root);
        [[nodiscard]] std::expected<std::filesystem::path, Error> relativePath(const std::filesystem::path& path) const;
        std::filesystem::path                                     m_Root;
        std::optional<VpkArchive>                                 m_Archive;
        mutable std::filesystem::path                             m_Materialized;
    };
} // namespace vultra
