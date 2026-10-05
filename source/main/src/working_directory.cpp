#include <vultra/main/packaged_resources.hpp>

namespace vultra
{
    // Keep path scoping independent of embedded packs so source-based C++ experiments need no pack symbols.
    ScopedWorkingDirectory::ScopedWorkingDirectory(const std::filesystem::path& path) :
        m_Previous(std::filesystem::current_path())
    {
        std::filesystem::current_path(path);
    }

    ScopedWorkingDirectory::~ScopedWorkingDirectory()
    {
        std::error_code error;
        std::filesystem::current_path(m_Previous, error);
    }
} // namespace vultra
