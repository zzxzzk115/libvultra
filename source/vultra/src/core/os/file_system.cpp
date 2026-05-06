#include "vultra/core/os/file_system.hpp"

#include <filesystem>
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
                throw std::runtime_error("Failed to open file: " + path.generic_string());
            }

            std::stringstream buffer;
            buffer << file.rdbuf();
            return buffer.str();
        }

        void FileSystem::writeFileAllText(const std::filesystem::path& path, std::string_view text)
        {
            if (path.has_parent_path())
            {
                std::filesystem::create_directories(path.parent_path());
            }

            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            if (!file.is_open())
            {
                throw std::runtime_error("Failed to write file: " + path.string());
            }

            file.write(text.data(), static_cast<std::streamsize>(text.size()));
        }
    } // namespace os
} // namespace vultra