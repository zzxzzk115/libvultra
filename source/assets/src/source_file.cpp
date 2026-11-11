#include <vultra/assets/asset_source.hpp>
#include <vultra/assets/source_file.hpp>

#include <fstream>

namespace vultra
{
    std::expected<std::vector<std::byte>, Error>
    readSourceFile(const std::filesystem::path& path, const SourceObserver& observer, const AssetSource* source)
    {
        if (source)
        {
            auto bytes = source->read(path);
            if (!bytes)
            {
                return std::unexpected(bytes.error());
            }
            if (bytes->empty())
            {
                return makeError("Cannot read empty asset source: {}", path.string());
            }
            if (observer)
            {
                const auto resolved = source->resolve(path);
                if (!resolved)
                {
                    return std::unexpected(resolved.error());
                }
                if (auto result = observer(*resolved, *bytes); !result)
                {
                    return std::unexpected(result.error());
                }
            }
            return bytes;
        }
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file || file.tellg() <= 0)
        {
            return makeError("Cannot read asset source: {}", path.string());
        }
        std::vector<std::byte> bytes(static_cast<size_t>(file.tellg()));
        file.seekg(0);
        if (!file.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size())))
        {
            return makeError("Truncated asset source: {}", path.string());
        }
        if (observer)
        {
            if (auto result = observer(path, bytes); !result)
            {
                return std::unexpected(result.error());
            }
        }
        return bytes;
    }
} // namespace vultra
