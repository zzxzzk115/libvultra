#include <vultra/app/packaged_resources.hpp>
#include <vultra/assets/vpk_archive.hpp>
#include <vultra/core/base/stable_id.hpp>
#include <vultra/platform/os/file.hpp>

#include <format>
#include <span>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#else
extern "C" const std::byte vultra_builtin_pack_start[];
extern "C" const std::byte vultra_builtin_pack_end[];
#endif

namespace vultra
{
    namespace
    {
        std::expected<std::span<const std::byte>, Error> builtinPack()
        {
#if defined(_WIN32)
            HMODULE module = nullptr;
            if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                    reinterpret_cast<LPCWSTR>(&builtinPack),
                                    &module))
            {
                return makeError("Find module containing the builtin pack");
            }
            const auto  resource = FindResourceW(module, MAKEINTRESOURCEW(101), MAKEINTRESOURCEW(10)); // RT_RCDATA
            const auto  loaded   = resource ? LoadResource(module, resource) : nullptr;
            const auto* data     = loaded ? static_cast<const std::byte*>(LockResource(loaded)) : nullptr;
            const auto  size     = resource ? SizeofResource(module, resource) : 0;
            if (!data || size == 0)
            {
                return makeError("Executable builtin pack is missing");
            }
            return std::span<const std::byte> {data, size};
#else
            const auto* begin = &vultra_builtin_pack_start[0];
            const auto* end   = &vultra_builtin_pack_end[0];
            if (end <= begin)
            {
                return makeError("Executable builtin pack is empty");
            }
            return std::span<const std::byte> {begin, size_t(end - begin)};
#endif
        }

        // FNV-1a of the pack, which names what was extracted from it.
        std::string packHash(std::span<const std::byte> bytes)
        {
            uint64_t hash = 14695981039346656037ull;
            for (const auto byte : bytes)
            {
                hash = (hash ^ std::to_integer<uint8_t>(byte)) * 1099511628211ull;
            }
            return std::format("{:016x}", hash);
        }
    } // namespace

    PackagedResources::PackagedResources(std::filesystem::path root, std::string shaderHash) :
        m_Root(std::move(root)),
        m_EngineRoot(m_Root / "engine"),
        m_ShaderHash(std::move(shaderHash))
    {
    }

    std::expected<PackagedResources, Error> PackagedResources::create()
    {
        const auto bytes = builtinPack();
        if (!bytes)
        {
            return std::unexpected(bytes.error());
        }
        const auto      hash = packHash(*bytes);
        std::error_code error;
        const auto      temporary = std::filesystem::temp_directory_path(error);
        if (error)
        {
            return makeError("Query temporary directory: {}", error.message());
        }
        // From here the object owns the directory and removes it on every failure below.
        PackagedResources resources(temporary / ("vultra-resources-" + StableId::generate().toString()), hash);
        std::filesystem::create_directories(resources.m_Root, error);
        if (error)
        {
            return makeError("Create {}: {}", resources.m_Root.string(), error.message());
        }
        const auto file = resources.m_Root / "builtin.vpk";
        if (auto written = writeFileAtomically(file, *bytes); !written)
        {
            return std::unexpected(written.error());
        }
        const auto archive = VpkArchive::create(file);
        if (!archive)
        {
            return std::unexpected(archive.error());
        }
        if (auto extracted = archive->extractTo(resources.m_EngineRoot); !extracted)
        {
            return std::unexpected(extracted.error());
        }
        return resources;
    }

    PackagedResources::PackagedResources(PackagedResources&& other) noexcept :
        m_Root(std::exchange(other.m_Root, {})),
        m_EngineRoot(std::exchange(other.m_EngineRoot, {})),
        m_ShaderHash(std::exchange(other.m_ShaderHash, {}))
    {
    }

    PackagedResources& PackagedResources::operator=(PackagedResources&& other) noexcept
    {
        if (this != &other)
        {
            removeFiles();
            m_Root       = std::exchange(other.m_Root, {});
            m_EngineRoot = std::exchange(other.m_EngineRoot, {});
            m_ShaderHash = std::exchange(other.m_ShaderHash, {});
        }
        return *this;
    }

    PackagedResources::~PackagedResources()
    {
        removeFiles();
    }

    void PackagedResources::removeFiles()
    {
        if (!m_Root.empty())
        {
            std::error_code error;
            std::filesystem::remove_all(m_Root, error);
        }
    }

    const std::filesystem::path& PackagedResources::engineRoot() const
    {
        return m_EngineRoot;
    }

    const std::string& PackagedResources::shaderHash() const
    {
        return m_ShaderHash;
    }

} // namespace vultra

namespace vultra
{
    std::expected<std::filesystem::path, Error> extractBuiltinPack(const std::filesystem::path& cache)
    {
        const auto bytes = builtinPack();
        if (!bytes)
        {
            return std::unexpected(bytes.error());
        }
        const auto      target = cache / packHash(*bytes);
        std::error_code error;
        if (std::filesystem::is_directory(target, error))
        {
            return target;
        }
        // Extracted beside the target and renamed, so a directory of that name is always complete.
        const auto staging = cache / ("staging-" + StableId::generate().toString());
        const auto archive = VpkArchive::create(staging, *bytes);
        if (!archive)
        {
            return std::unexpected(archive.error());
        }
        if (auto extracted = archive->extractTo(staging); !extracted)
        {
            std::filesystem::remove_all(staging, error);
            return std::unexpected(extracted.error());
        }
        std::filesystem::rename(staging, target, error);
        if (error)
        {
            const auto reason = error.message();
            std::filesystem::remove_all(staging, error);
            // Another process extracting the same pack finished first.
            if (std::filesystem::is_directory(target, error))
            {
                return target;
            }
            return makeError("Extract the builtin pack to {}: {}", target.string(), reason);
        }
        return target;
    }
} // namespace vultra
