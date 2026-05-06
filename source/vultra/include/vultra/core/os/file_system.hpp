#pragma once

#include <filesystem>

namespace vultra
{
    namespace os
    {
        class FileSystem final
        {
        public:
            static std::string readFileAllText(const std::filesystem::path& path);
            static void        writeFileAllText(const std::filesystem::path& path, std::string_view text);
        };
    } // namespace os
} // namespace vultra