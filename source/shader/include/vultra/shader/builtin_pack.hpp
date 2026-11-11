#pragma once

#include <vultra/core/base/error.hpp>

#include <expected>
#include <filesystem>

namespace vultra
{
    // Cooks library-owned shaders and their attribution, without an engine project or type catalog.
    [[nodiscard]] std::expected<void, Error> packBuiltins(const std::filesystem::path& libraryRoot,
                                                          const std::filesystem::path& output);
} // namespace vultra
