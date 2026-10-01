#pragma once

#include <filesystem>
#include <span>

namespace vultra
{
    // Publish a complete file; readers see either the previous file or the replacement.
    void writeFileAtomically(const std::filesystem::path& path, std::span<const std::byte> bytes);
} // namespace vultra
