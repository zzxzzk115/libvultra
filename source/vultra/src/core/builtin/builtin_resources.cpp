#include "vultra/core/builtin/builtin_resources.hpp"

#include <vfilesystem/interfaces/ifile.hpp>
#include <vfilesystem/interfaces/ifilesystem.hpp>

#include <unordered_map>

namespace vultra::builtin
{
    namespace
    {
        // Function-local static: safe to assign from a static initializer in another TU
        // (the per-binary builtin_pack_mount.cpp self-registers at startup).
        std::shared_ptr<vfilesystem::IFileSystem>& source()
        {
            static std::shared_ptr<vfilesystem::IFileSystem> s;
            return s;
        }

        std::vector<std::string>& entries()
        {
            static std::vector<std::string> e;
            return e;
        }
    } // namespace

    void setSource(std::shared_ptr<vfilesystem::IFileSystem> backend, std::vector<std::string> logicalPaths)
    {
        source()  = std::move(backend);
        entries() = std::move(logicalPaths);
    }

    std::vector<std::string> list(std::string_view prefix)
    {
        std::vector<std::string> out;
        for (const auto& p : entries())
            if (p.size() >= prefix.size() && std::string_view(p).substr(0, prefix.size()) == prefix)
                out.push_back(p);
        return out;
    }

    std::span<const std::uint8_t> cachedBytes(std::string_view logicalPath)
    {
        static std::unordered_map<std::string, std::vector<std::uint8_t>> cache;
        auto                                                              it = cache.find(std::string(logicalPath));
        if (it == cache.end())
        {
            std::vector<std::byte>    raw;
            std::vector<std::uint8_t> bytes;
            if (read(logicalPath, raw) && !raw.empty())
                bytes.assign(reinterpret_cast<const std::uint8_t*>(raw.data()),
                             reinterpret_cast<const std::uint8_t*>(raw.data()) + raw.size());
            it = cache.emplace(std::string(logicalPath), std::move(bytes)).first;
        }
        return std::span<const std::uint8_t>(it->second.data(), it->second.size());
    }

    std::vector<std::pair<std::string, std::string>> shaderIncludeSources()
    {
        constexpr std::string_view prefix = "shaders/include/";
        std::vector<std::pair<std::string, std::string>> out;
        for (const auto& logical : list(prefix))
        {
            std::vector<std::byte> raw;
            if (!read(logical, raw) || raw.empty())
                continue;
            // virtualPath drops the leading "shaders/" -> "include/...".
            out.emplace_back(logical.substr(std::string_view {"shaders/"}.size()),
                             std::string {reinterpret_cast<const char*>(raw.data()), raw.size()});
        }
        return out;
    }

    bool hasSource() { return static_cast<bool>(source()); }

    bool read(std::string_view logicalPath, std::vector<std::byte>& out)
    {
        if (!source())
            return false;

        auto opened = source()->open(logicalPath, vfilesystem::FileMode::eRead);
        if (!opened)
            return false;

        out = opened.value()->readAllBytes();
        return true;
    }
} // namespace vultra::builtin
