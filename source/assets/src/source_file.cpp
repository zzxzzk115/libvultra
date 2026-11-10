#include "asset_file.hpp"

#include <vultra/assets/asset_source.hpp>
#include <vultra/assets/source_file.hpp>

#include <stdexcept>

namespace vultra
{
    std::expected<std::vector<std::byte>, std::string> readSourceFileExpected(const std::filesystem::path&  path,
                                                                              const ExpectedSourceObserver& observer,
                                                                              const AssetSource*            source)
    {
        auto identity =
            source ? source->resolveExpected(path) : std::expected<std::filesystem::path, std::string>(path);
        if (!identity)
        {
            return std::unexpected(identity.error());
        }
        auto bytes = source ? source->readExpected(path) : detail::readAssetFileExpected(path);
        if (!bytes)
        {
            return std::unexpected(bytes.error());
        }
        if (bytes->empty())
        {
            return std::unexpected("Cannot read empty asset source: " + path.string());
        }
        if (observer)
        {
            if (auto observed = observer(*identity, *bytes); !observed)
            {
                return std::unexpected(observed.error());
            }
        }
        return bytes;
    }

    std::vector<std::byte>
    readSourceFile(const std::filesystem::path& path, const SourceObserver& observer, const AssetSource* source)
    {
        ExpectedSourceObserver observe;
        if (observer)
        {
            observe = [&](const auto& identity, auto bytes) -> std::expected<void, std::string>
            {
                observer(identity, bytes); // Legacy callback boundary.
                return {};
            };
        }
        auto bytes = readSourceFileExpected(path, observe, source);
        if (!bytes)
        {
            throw std::runtime_error(bytes.error());
        }
        return std::move(*bytes);
    }
} // namespace vultra
