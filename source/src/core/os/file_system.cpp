#include "vultra/core/os/file_system.hpp"

#include <fstream>
#include <sstream>

namespace vultra
{
    namespace os
    {
        std::string FileSystem::readFileAllText(const std::filesystem::path& path)
        {
            const std::ifstream file(path);
            if (!file.is_open())
            {
                throw std::runtime_error("Failed to open file: " + path.string());
            }

            std::stringstream buffer;
            buffer << file.rdbuf();
            return buffer.str();
        }
    } // namespace os
} // namespace vultra