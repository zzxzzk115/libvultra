#include <vultra/assets/source_file.hpp>

#include <fstream>
#include <stdexcept>

namespace vultra
{
    std::vector<std::byte> readSourceFile(const std::filesystem::path& path, const SourceObserver& observer)
    {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file || file.tellg() <= 0)
        {
            throw std::runtime_error("Cannot read asset source: " + path.string());
        }
        std::vector<std::byte> bytes(static_cast<size_t>(file.tellg()));
        file.seekg(0);
        if (!file.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size())))
        {
            throw std::runtime_error("Truncated asset source: " + path.string());
        }
        if (observer)
        {
            observer(path, bytes);
        }
        return bytes;
    }
} // namespace vultra
