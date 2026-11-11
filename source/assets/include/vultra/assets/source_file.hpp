#pragma once

#include <vultra/core/base/error.hpp>

#include <expected>
#include <filesystem>
#include <functional>
#include <span>
#include <vector>

namespace vultra
{
    class AssetSource;
    // Called with the exact consumed bytes, borrowed only for this call, including external dependencies.
    // Importers serialize notifications, but may call from a worker; callbacks must not access UI or GPU state.
    // A returned error aborts the read that reported the bytes.
    using SourceObserver =
        std::function<std::expected<void, Error>(const std::filesystem::path&, std::span<const std::byte>)>;
    [[nodiscard]] std::expected<std::vector<std::byte>, Error> readSourceFile(const std::filesystem::path& path,
                                                                              const SourceObserver& observer = {},
                                                                              const AssetSource*    source   = nullptr);
} // namespace vultra
