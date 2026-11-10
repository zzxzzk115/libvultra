#pragma once

#include <expected>
#include <filesystem>
#include <fstream>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace vultra::detail
{
    // Raw assets may be empty; format readers decide whether zero bytes are meaningful.
    inline std::expected<std::vector<std::byte>, std::string> readAssetFileExpected(const std::filesystem::path& path)
    {
        if (path.native().find(std::filesystem::path::value_type(0)) != std::filesystem::path::string_type::npos)
        {
            return std::unexpected("Asset path contains a NUL character");
        }
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error))
        {
            return std::unexpected("Asset is not a readable regular file: " + path.string() + ": " + error.message());
        }
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        const auto    length = input ? input.tellg() : std::streampos(-1);
        if (length < std::streamoff(0) || uint64_t(length) > std::numeric_limits<size_t>::max() ||
            uint64_t(length) > uint64_t(std::numeric_limits<std::streamsize>::max()))
        {
            return std::unexpected("Missing or oversized asset: " + path.string());
        }
        std::vector<std::byte> bytes;
        if (uint64_t(length) > bytes.max_size())
        {
            return std::unexpected("Asset exceeds container size: " + path.string());
        }
        bytes.resize(static_cast<size_t>(length));
        input.seekg(0);
        if (!bytes.empty())
        {
            input.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size()));
        }
        if (!input || input.peek() != std::char_traits<char>::eof() || input.bad())
        {
            return std::unexpected("Asset read failed or changed size: " + path.string());
        }
        return bytes;
    }
} // namespace vultra::detail
