#include <vultra/assets/asset_source.hpp>
#include <vultra/assets/source_file.hpp>
#include <vultra/core/base/stable_id.hpp>
#include <vultra/platform/os/file.hpp>

#include <utility>

namespace vultra
{
    namespace
    {
        std::string pathText(const std::filesystem::path& path)
        {
            const auto text = path.generic_u8string();
            return {text.begin(), text.end()};
        }
    } // namespace

    AssetSource::AssetSource(std::filesystem::path root) :
        m_Root(std::move(root))
    {
    }

    std::expected<std::unique_ptr<AssetSource>, Error> AssetSource::create(const std::filesystem::path& root)
    {
        std::error_code error;
        auto            canonical = std::filesystem::canonical(root, error);
        if (error)
        {
            return makeError("Resolve asset source root {}: {}", root.string(), error.message());
        }
        if (!std::filesystem::is_directory(canonical, error))
        {
            return makeError("Asset source root must be a directory: {}", canonical.string());
        }
        return std::unique_ptr<AssetSource>(new AssetSource(std::move(canonical)));
    }

    AssetSource::AssetSource(VpkArchive archive) :
        m_Root(archive.file()),
        m_Archive(std::move(archive))
    {
    }

    AssetSource::~AssetSource()
    {
        if (!m_Materialized.empty())
        {
            std::error_code error;
            std::filesystem::remove_all(m_Materialized, error);
        }
    }

    const std::filesystem::path& AssetSource::root() const
    {
        return m_Root;
    }

    std::expected<std::filesystem::path, Error> AssetSource::relativePath(const std::filesystem::path& path) const
    {
        const auto            absolute = path.is_absolute() ? path : m_Root / path;
        std::filesystem::path resolved;
        if (m_Archive)
        {
            resolved = absolute.lexically_normal();
        }
        else
        {
            std::error_code error;
            resolved = std::filesystem::weakly_canonical(absolute, error);
            if (error)
            {
                return makeError("Resolve asset path {}: {}", path.string(), error.message());
            }
        }
        const auto relative = resolved.lexically_relative(m_Root);
        if (relative.empty() || relative == "." || relative.is_absolute())
        {
            return makeError("Asset path must name a file within its source: {}", path.string());
        }
        for (const auto& part : relative)
        {
            if (part == "..")
            {
                return makeError("Asset path leaves its source: {}", path.string());
            }
        }
        return relative;
    }

    std::expected<std::filesystem::path, Error> AssetSource::resolve(const std::filesystem::path& path) const
    {
        const auto relative = relativePath(path);
        if (!relative)
        {
            return std::unexpected(relative.error());
        }
        return m_Root / *relative;
    }

    std::expected<bool, Error> AssetSource::contains(const std::filesystem::path& path) const
    {
        const auto relative = relativePath(path);
        if (!relative)
        {
            return std::unexpected(relative.error());
        }
        if (m_Archive)
        {
            return m_Archive->contains(pathText(*relative));
        }
        std::error_code error;
        const auto      status = std::filesystem::status(m_Root / *relative, error);
        // A missing file is an answer; only a failed query is an error.
        if (status.type() == std::filesystem::file_type::none)
        {
            return makeError("Query asset {}: {}", path.string(), error.message());
        }
        return std::filesystem::is_regular_file(status);
    }

    std::expected<uint64_t, Error> AssetSource::size(const std::filesystem::path& path) const
    {
        const auto relative = relativePath(path);
        if (!relative)
        {
            return std::unexpected(relative.error());
        }
        if (m_Archive)
        {
            return m_Archive->size(pathText(*relative));
        }
        std::error_code error;
        const auto      size = std::filesystem::file_size(m_Root / *relative, error);
        if (error)
        {
            return makeError("Query asset size {}: {}", path.string(), error.message());
        }
        return uint64_t(size);
    }

    std::expected<std::vector<std::byte>, Error> AssetSource::read(const std::filesystem::path& path) const
    {
        const auto relative = relativePath(path);
        if (!relative)
        {
            return std::unexpected(relative.error());
        }
        return m_Archive ? m_Archive->read(pathText(*relative)) : readSourceFile(m_Root / *relative);
    }

    std::expected<std::filesystem::path, Error> AssetSource::materialize(const std::filesystem::path& path) const
    {
        const auto relative = relativePath(path);
        if (!relative)
        {
            return std::unexpected(relative.error());
        }
        if (!m_Archive)
        {
            return m_Root / *relative;
        }
        std::error_code error;
        if (m_Materialized.empty())
        {
            const auto directory =
                std::filesystem::temp_directory_path(error) / ("vultra-files-" + StableId::generate().toString());
            if (!error)
            {
                std::filesystem::create_directory(directory, error);
            }
            if (error)
            {
                return makeError("Create materialization directory: {}", error.message());
            }
            m_Materialized = directory;
        }
        const auto output = m_Materialized / *relative;
        const bool exists = std::filesystem::exists(output, error);
        if (error)
        {
            return makeError("Query materialized asset {}: {}", output.string(), error.message());
        }
        if (!exists)
        {
            const auto bytes = read(path);
            if (!bytes)
            {
                return std::unexpected(bytes.error());
            }
            std::filesystem::create_directories(output.parent_path(), error);
            if (error)
            {
                return makeError("Create directory {}: {}", output.parent_path().string(), error.message());
            }
            if (auto result = writeFileAtomically(output, *bytes); !result)
            {
                return std::unexpected(result.error());
            }
        }
        return output;
    }
} // namespace vultra
