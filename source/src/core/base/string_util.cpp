#include "vultra/core/base/string_util.hpp"

#include <array>
#include <format>

namespace vultra
{
    namespace util
    {
        std::string formatBytes(std::size_t bytes)
        {
            static constexpr std::array kUnits {"B", "KiB", "MiB", "GiB"};

            std::size_t i {0};
            auto        size = static_cast<double>(bytes);

            // https://gist.github.com/dgoguerra/7194777
            if (bytes >= 1024)
            {
                for (; (bytes / 1024) > 0 && (i < kUnits.size() - 1); i++, bytes /= 1024)
                {
                    size = bytes / 1024.0;
                }
            }
            return std::format("{:.2f} {}", size, kUnits[i]);
        }

    } // namespace util
} // namespace vultra