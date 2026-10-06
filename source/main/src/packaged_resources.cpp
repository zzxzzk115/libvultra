#include <vultra/assets/vpk_archive.hpp>
#include <vultra/core/base/stable_id.hpp>
#include <vultra/main/packaged_resources.hpp>
#include <vultra/platform/os/file.hpp>

#include <format>
#include <span>
#include <stdexcept>

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
        std::span<const std::byte> builtinPack()
        {
#if defined(_WIN32)
            HMODULE module = nullptr;
            if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                    reinterpret_cast<LPCWSTR>(&builtinPack),
                                    &module))
            {
                throw std::runtime_error("Find module containing the builtin pack");
            }
            const auto  resource = FindResourceW(module, MAKEINTRESOURCEW(101), MAKEINTRESOURCEW(10)); // RT_RCDATA
            const auto  loaded   = resource ? LoadResource(module, resource) : nullptr;
            const auto* data     = loaded ? static_cast<const std::byte*>(LockResource(loaded)) : nullptr;
            const auto  size     = resource ? SizeofResource(module, resource) : 0;
            if (!data || size == 0)
            {
                throw std::runtime_error("Executable builtin pack is missing");
            }
            return {data, size};
#else
            const auto* begin = &vultra_builtin_pack_start[0];
            const auto* end   = &vultra_builtin_pack_end[0];
            if (end <= begin)
            {
                throw std::runtime_error("Executable builtin pack is empty");
            }
            return {begin, size_t(end - begin)};
#endif
        }
    } // namespace

    PackagedResources::PackagedResources() :
        m_Root(std::filesystem::temp_directory_path() / ("vultra-resources-" + StableId::generate().toString())),
        m_EngineRoot(m_Root / "engine")
    {
        const auto bytes = builtinPack();
        uint64_t   hash  = 14695981039346656037ull;
        for (const auto byte : bytes)
        {
            hash = (hash ^ std::to_integer<uint8_t>(byte)) * 1099511628211ull;
        }
        m_ShaderHash = std::format("{:016x}", hash);
        std::filesystem::create_directories(m_Root);
        try
        {
            const auto file = m_Root / "builtin.vpk";
            writeFileAtomically(file, bytes);
            VpkArchive(file).extractTo(m_EngineRoot);
        }
        catch (...)
        {
            std::error_code error;
            std::filesystem::remove_all(m_Root, error);
            throw;
        }
    }

    PackagedResources::~PackagedResources()
    {
        std::error_code error;
        std::filesystem::remove_all(m_Root, error);
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
