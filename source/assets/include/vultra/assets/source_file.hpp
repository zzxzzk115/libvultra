#pragma once

#include <filesystem>
#include <functional>
#include <span>
#include <vector>

namespace vultra
{
    // Called with the exact consumed bytes, including external dependencies. Calls are serialized,
    // but may execute on an import worker; callbacks must not access thread-affine UI or GPU state.
    using SourceObserver = std::function<void(const std::filesystem::path&, std::span<const std::byte>)>;
    std::vector<std::byte> readSourceFile(const std::filesystem::path& path, const SourceObserver& observer = {});
} // namespace vultra
