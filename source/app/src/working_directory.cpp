#include <vultra/app/packaged_resources.hpp>

#include <utility>

namespace vultra
{
    // Keep path scoping independent of embedded packs so source-based C++ experiments need no pack symbols.
    ScopedWorkingDirectory::ScopedWorkingDirectory(std::filesystem::path previous) :
        m_Previous(std::move(previous))
    {
    }

    std::expected<ScopedWorkingDirectory, Error> ScopedWorkingDirectory::enter(const std::filesystem::path& path)
    {
        std::error_code error;
        auto            previous = std::filesystem::current_path(error);
        if (error)
        {
            return makeError("Query working directory: {}", error.message());
        }
        std::filesystem::current_path(path, error);
        if (error)
        {
            return makeError("Enter working directory {}: {}", path.string(), error.message());
        }
        return ScopedWorkingDirectory(std::move(previous));
    }

    ScopedWorkingDirectory::ScopedWorkingDirectory(ScopedWorkingDirectory&& other) noexcept :
        m_Previous(std::exchange(other.m_Previous, std::nullopt))
    {
    }

    ScopedWorkingDirectory::~ScopedWorkingDirectory()
    {
        if (m_Previous)
        {
            std::error_code error;
            std::filesystem::current_path(*m_Previous, error);
        }
    }
} // namespace vultra
