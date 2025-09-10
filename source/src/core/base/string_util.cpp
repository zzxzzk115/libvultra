#include "vultra/core/base/string_util.hpp"

#include <algorithm>
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

        std::string toupper_str(const std::string& str)
        {
            std::string result = str;
            std::transform(
                result.begin(), result.end(), result.begin(), [](unsigned char c) { return std::toupper(c); });
            return result;
        }

        std::vector<std::string> split_str(const std::string& str, char delimiter)
        {
            std::vector<std::string> parts;
            std::string              currentPart;

            for (char ch : str)
            {
                if (ch == delimiter)
                {
                    if (!currentPart.empty())
                    {
                        parts.push_back(currentPart);
                        currentPart.clear();
                    }
                }
                else
                {
                    currentPart += ch;
                }
            }

            if (!currentPart.empty())
            {
                parts.push_back(currentPart);
            }

            return parts;
        }
    } // namespace util
} // namespace vultra