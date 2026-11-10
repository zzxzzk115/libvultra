#include "asset_file.hpp"

#include <vultra/assets/asset_source.hpp>
#include <vultra/core/base/stable_id.hpp>
#include <vultra/platform/os/file.hpp>

#include <stdexcept>
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

        bool missing(const std::error_code& error)
        {
            return error == std::errc::no_such_file_or_directory || error == std::errc::not_a_directory;
        }
    } // namespace

    std::expected<void, std::string> AssetSource::initialize(const std::filesystem::path& root)
    {
        if (root.empty() ||
            root.native().find(std::filesystem::path::value_type(0)) != std::filesystem::path::string_type::npos)
        {
            return std::unexpected("Asset source root requires a nonempty path without NUL characters");
        }
        std::error_code error;
        auto            canonical = std::filesystem::canonical(root, error);
        if (error)
        {
            return std::unexpected("Resolve asset source root: " + error.message());
        }
        if (!std::filesystem::is_directory(canonical, error) || error)
        {
            return std::unexpected("Asset source root must be a directory: " + canonical.string());
        }
        m_Root = std::move(canonical);
        return {};
    }

    std::expected<void, std::string> AssetSource::initialize(VpkArchive archive)
    {
        if (archive.file().empty() || !archive.file().is_absolute())
        {
            return std::unexpected("Asset package requires an initialized archive identity");
        }
        m_Root = archive.file();
        m_Archive.emplace(std::move(archive));
        return {};
    }

    std::expected<std::unique_ptr<AssetSource>, std::string> AssetSource::create(const std::filesystem::path& root)
    {
        auto source = std::unique_ptr<AssetSource>(new AssetSource(Uninitialized {}));
        if (auto ready = source->initialize(root); !ready)
        {
            return std::unexpected(ready.error());
        }
        return source;
    }

    std::expected<std::unique_ptr<AssetSource>, std::string> AssetSource::create(VpkArchive archive)
    {
        auto source = std::unique_ptr<AssetSource>(new AssetSource(Uninitialized {}));
        if (auto ready = source->initialize(std::move(archive)); !ready)
        {
            return std::unexpected(ready.error());
        }
        return source;
    }

    AssetSource::AssetSource(std::filesystem::path root) :
        AssetSource(Uninitialized {})
    {
        if (auto ready = initialize(root); !ready)
        {
            throw std::runtime_error(ready.error());
        }
    }

    AssetSource::AssetSource(VpkArchive archive) :
        AssetSource(Uninitialized {})
    {
        if (auto ready = initialize(std::move(archive)); !ready)
        {
            throw std::runtime_error(ready.error());
        }
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

    std::expected<std::filesystem::path, std::string>
    AssetSource::relativePathExpected(const std::filesystem::path& path) const
    {
        if (path.native().find(std::filesystem::path::value_type(0)) != std::filesystem::path::string_type::npos)
        {
            return std::unexpected("Asset path contains a NUL character");
        }
        auto resolved = (path.is_absolute() ? path : m_Root / path).lexically_normal();
        if (!m_Archive)
        {
            std::error_code error;
            resolved = std::filesystem::weakly_canonical(resolved, error);
            if (error)
            {
                return std::unexpected("Resolve asset path: " + error.message());
            }
        }
        const auto relative = resolved.lexically_relative(m_Root);
        if (relative.empty() || relative == "." || relative.is_absolute())
            return std::unexpected("Asset path must name a file within its source: " + path.string());
        for (const auto& part : relative)
        {
            if (part == "..")
                return std::unexpected("Asset path leaves its source: " + path.string());
        }
        return relative;
    }

    std::expected<std::filesystem::path, std::string>
    AssetSource::resolveExpected(const std::filesystem::path& path) const
    {
        auto relative = relativePathExpected(path);
        if (!relative)
        {
            return std::unexpected(relative.error());
        }
        return m_Root / *relative;
    }

    std::expected<bool, std::string> AssetSource::containsExpected(const std::filesystem::path& path) const
    {
        auto relative = relativePathExpected(path);
        if (!relative)
        {
            return std::unexpected(relative.error());
        }
        if (m_Archive)
        {
            return m_Archive->containsExpected(pathText(*relative));
        }
        std::error_code error;
        const bool      regular = std::filesystem::is_regular_file(m_Root / *relative, error);
        if (error && !missing(error))
        {
            return std::unexpected("Inspect asset: " + error.message());
        }
        return regular;
    }

    std::expected<uint64_t, std::string> AssetSource::sizeExpected(const std::filesystem::path& path) const
    {
        auto relative = relativePathExpected(path);
        if (!relative)
        {
            return std::unexpected(relative.error());
        }
        if (m_Archive)
        {
            return m_Archive->sizeExpected(pathText(*relative));
        }
        std::error_code error;
        const auto      size = std::filesystem::file_size(m_Root / *relative, error);
        if (error)
        {
            return std::unexpected("Query asset size: " + error.message());
        }
        return size;
    }

    std::expected<std::vector<std::byte>, std::string>
    AssetSource::readExpected(const std::filesystem::path& path) const
    {
        auto relative = relativePathExpected(path);
        if (!relative)
        {
            return std::unexpected(relative.error());
        }
        return m_Archive ? m_Archive->readExpected(pathText(*relative)) :
                           detail::readAssetFileExpected(m_Root / *relative);
    }

    std::expected<std::filesystem::path, std::string>
    AssetSource::materializeExpected(const std::filesystem::path& path) const
    {
        auto relative = relativePathExpected(path);
        if (!relative)
        {
            return std::unexpected(relative.error());
        }
        if (!m_Archive)
        {
            return m_Root / *relative;
        }
        std::error_code error;
        auto            directory = m_Materialized;
        const bool      fresh     = directory.empty();
        if (fresh)
        {
            directory = std::filesystem::temp_directory_path(error);
            if (error)
            {
                return std::unexpected("Find asset materialization directory: " + error.message());
            }
            directory /= "vultra-files-" + StableId::generate().toString();
            if (!std::filesystem::create_directory(directory, error) || error)
            {
                return std::unexpected("Create asset materialization directory: " + error.message());
            }
        }
        const auto fail = [&](std::string reason) -> std::expected<std::filesystem::path, std::string>
        {
            if (fresh)
            {
                std::error_code cleanup;
                std::filesystem::remove_all(directory, cleanup);
                if (cleanup)
                {
                    reason += "; remove candidate directory: " + cleanup.message();
                }
            }
            return std::unexpected(std::move(reason));
        };
        const auto output = directory / *relative;
        const bool exists = std::filesystem::exists(output, error);
        if (error)
        {
            return fail("Inspect materialized asset: " + error.message());
        }
        if (exists && (!std::filesystem::is_regular_file(output, error) || error))
        {
            return fail("Materialized asset is not a regular file: " + output.string());
        }
        if (!exists)
        {
            auto bytes = m_Archive->readExpected(pathText(*relative));
            if (!bytes)
            {
                return fail(bytes.error());
            }
            std::filesystem::create_directories(output.parent_path(), error);
            if (error)
            {
                return fail("Create materialized asset directory: " + error.message());
            }
            if (auto written = writeFileAtomicallyExpected(output, *bytes); !written)
            {
                return fail(written.error());
            }
        }
        if (fresh)
        {
            m_Materialized = directory;
        }
        return output;
    }

    std::filesystem::path AssetSource::resolve(const std::filesystem::path& path) const
    {
        auto result = resolveExpected(path);
        if (!result)
        {
            throw std::runtime_error(result.error());
        }
        return std::move(*result);
    }

    bool AssetSource::contains(const std::filesystem::path& path) const
    {
        auto result = containsExpected(path);
        if (!result)
        {
            throw std::runtime_error(result.error());
        }
        return *result;
    }

    uint64_t AssetSource::size(const std::filesystem::path& path) const
    {
        auto result = sizeExpected(path);
        if (!result)
        {
            throw std::runtime_error(result.error());
        }
        return *result;
    }

    std::vector<std::byte> AssetSource::read(const std::filesystem::path& path) const
    {
        auto result = readExpected(path);
        if (!result)
        {
            throw std::runtime_error(result.error());
        }
        if (!m_Archive && result->empty())
        {
            throw std::runtime_error("Cannot read empty asset source: " + path.string());
        }
        return std::move(*result);
    }

    std::filesystem::path AssetSource::materialize(const std::filesystem::path& path) const
    {
        auto result = materializeExpected(path);
        if (!result)
        {
            throw std::runtime_error(result.error());
        }
        return std::move(*result);
    }
} // namespace vultra
