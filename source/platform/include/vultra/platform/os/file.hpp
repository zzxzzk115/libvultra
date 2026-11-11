#pragma once

#include <vultra/core/base/error.hpp>

#include <expected>
#include <filesystem>
#include <span>

namespace vultra
{
    // Publish a complete file; readers see either the previous file or the replacement.
    [[nodiscard]] std::expected<void, Error> writeFileAtomically(const std::filesystem::path& path,
                                                                 std::span<const std::byte>   bytes);
} // namespace vultra
