#pragma once

#include <vultra/assets/vpk_archive.hpp>

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
        explicit AssetSource(std::filesystem::path root);
        explicit AssetSource(VpkArchive archive);
        static std::expected<std::unique_ptr<AssetSource>, std::string> create(const std::filesystem::path& root);
        static std::expected<std::unique_ptr<AssetSource>, std::string> create(VpkArchive archive);
        ~AssetSource();
        AssetSource(const AssetSource&)            = delete;
        AssetSource& operator=(const AssetSource&) = delete;

        const std::filesystem::path& root() const;
        std::filesystem::path        resolve(const std::filesystem::path& path) const;
        bool                         contains(const std::filesystem::path& path) const;
        uint64_t                     size(const std::filesystem::path& path) const;
        std::vector<std::byte>       read(const std::filesystem::path& path) const;
        // For loaders that require real files, such as native modules and .NET assemblies.
        // Returned paths expire with this source. This never extracts unrelated entries.
        std::filesystem::path materialize(const std::filesystem::path& path) const;
        std::expected<std::filesystem::path, std::string>  resolveExpected(const std::filesystem::path& path) const;
        std::expected<bool, std::string>                   containsExpected(const std::filesystem::path& path) const;
        std::expected<uint64_t, std::string>               sizeExpected(const std::filesystem::path& path) const;
        std::expected<std::vector<std::byte>, std::string> readExpected(const std::filesystem::path& path) const;
        std::expected<std::filesystem::path, std::string>  materializeExpected(const std::filesystem::path& path) const;

    private:
        struct Uninitialized
        {};

        explicit AssetSource(Uninitialized)
        {
        }

        std::expected<void, std::string>                  initialize(const std::filesystem::path& root);
        std::expected<void, std::string>                  initialize(VpkArchive archive);
        std::expected<std::filesystem::path, std::string> relativePathExpected(const std::filesystem::path& path) const;
        std::filesystem::path         m_Root;
        std::optional<VpkArchive>     m_Archive;
        mutable std::filesystem::path m_Materialized;
    };
} // namespace vultra
