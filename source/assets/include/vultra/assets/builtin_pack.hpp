#pragma once

#include <filesystem>

namespace vultra
{
    // Cooks library-owned shaders and their attribution, without an engine project or type catalog.
    void packBuiltins(const std::filesystem::path& libraryRoot, const std::filesystem::path& output);
} // namespace vultra
