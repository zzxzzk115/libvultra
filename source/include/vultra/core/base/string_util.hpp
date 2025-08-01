#pragma once

#include <string>

namespace vultra
{
    namespace util
    {
        [[nodiscard]] std::string formatBytes(std::size_t bytes);
    }
} // namespace vultra