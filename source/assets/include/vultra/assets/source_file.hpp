#pragma once

#include <filesystem>
#include <functional>
#include <span>
#include <vector>

namespace vultra
{
    class AssetSource;
    // Called with the exact consumed bytes, borrowed only for this call, including external dependencies.
    // Importers serialize notifications, but may call from a worker; callbacks must not access UI or GPU state.
    using SourceObserver = std::function<void(const std::filesystem::path&, std::span<const std::byte>)>;
    std::vector<std::byte> readSourceFile(const std::filesystem::path& path,
                                          const SourceObserver&        observer = {},
                                          const AssetSource*           source   = nullptr);
} // namespace vultra
