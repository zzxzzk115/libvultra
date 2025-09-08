#pragma once

#include <string>

namespace vultra
{
    namespace util
    {
        [[nodiscard]] std::string formatBytes(std::size_t bytes);

        [[nodiscard]] std::string toupper_str(const std::string& str);
    }
} // namespace vultra