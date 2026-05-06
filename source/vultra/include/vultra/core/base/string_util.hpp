#pragma once

#include <string>
#include <vector>

namespace vultra
{
    namespace util
    {
        [[nodiscard]] std::string formatBytes(std::size_t bytes);

        [[nodiscard]] std::string toupper_str(const std::string& str);

        [[nodiscard]] std::vector<std::string> split_str(const std::string& str, char delimiter);
    }
} // namespace vultra