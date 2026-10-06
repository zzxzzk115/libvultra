#include <vultra/assets/asset_source.hpp>
#include <vultra/assets/source_file.hpp>
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
    } // namespace

    AssetSource::AssetSource(std::filesystem::path root) :
        m_Root(std::filesystem::canonical(root))
    {
        if (!std::filesystem::is_directory(m_Root))
        {
            throw std::invalid_argument("Asset source root must be a directory: " + m_Root.string());
        }
    }

    AssetSource::AssetSource(VpkArchive archive) :
        m_Root(std::filesystem::absolute(archive.file()).lexically_normal()),
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

    std::filesystem::path AssetSource::relativePath(const std::filesystem::path& path) const
    {
        const auto resolved = m_Archive ? (path.is_absolute() ? path : m_Root / path).lexically_normal() :
                                          std::filesystem::weakly_canonical(path.is_absolute() ? path : m_Root / path);
        const auto relative = resolved.lexically_relative(m_Root);
        if (relative.empty() || relative == "." || relative.is_absolute())
        {
            throw std::invalid_argument("Asset path must name a file within its source: " + path.string());
        }
        for (const auto& part : relative)
        {
            if (part == "..")
            {
                throw std::invalid_argument("Asset path leaves its source: " + path.string());
            }
        }
        return relative;
    }

    std::filesystem::path AssetSource::resolve(const std::filesystem::path& path) const
    {
        return m_Root / relativePath(path);
    }

    bool AssetSource::contains(const std::filesystem::path& path) const
    {
        const auto relative = relativePath(path);
        return m_Archive ? m_Archive->contains(pathText(relative)) :
                           std::filesystem::is_regular_file(m_Root / relative);
    }

    uint64_t AssetSource::size(const std::filesystem::path& path) const
    {
        const auto relative = relativePath(path);
        return m_Archive ? m_Archive->size(pathText(relative)) : std::filesystem::file_size(m_Root / relative);
    }

    std::vector<std::byte> AssetSource::read(const std::filesystem::path& path) const
    {
        const auto relative = relativePath(path);
        return m_Archive ? m_Archive->read(pathText(relative)) : readSourceFile(m_Root / relative);
    }

    std::filesystem::path AssetSource::materialize(const std::filesystem::path& path) const
    {
        const auto relative = relativePath(path);
        if (!m_Archive)
        {
            return m_Root / relative;
        }
        if (m_Materialized.empty())
        {
            m_Materialized =
                std::filesystem::temp_directory_path() / ("vultra-files-" + StableId::generate().toString());
            std::filesystem::create_directory(m_Materialized);
        }
        const auto output = m_Materialized / relative;
        if (!std::filesystem::exists(output))
        {
            const auto bytes = read(path);
            std::filesystem::create_directories(output.parent_path());
            writeFileAtomically(output, bytes);
        }
        return output;
    }
} // namespace vultra
