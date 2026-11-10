#pragma once

#include <expected>
#include <filesystem>
#include <span>
#include <string>

namespace vultra
{
    // Publish a complete file; readers see either the previous file or the replacement.
    std::expected<void, std::string> writeFileAtomicallyExpected(const std::filesystem::path& path,
                                                                 std::span<const std::byte>   bytes);
    void writeFileAtomically(const std::filesystem::path& path, std::span<const std::byte> bytes); // Legacy boundary.
} // namespace vultra
