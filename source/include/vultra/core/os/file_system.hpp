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
        };
    } // namespace os
} // namespace vultra