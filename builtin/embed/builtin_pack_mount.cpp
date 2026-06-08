// Per-binary accessor for the embedded builtin.vpk. Compiled into each self-contained
// binary (editor, examples) by the `vultra.builtin_pack` rule, alongside the platform
// embed file (.rc / .S). Reads the embedded pack and installs it as the builtin:: source.

#include "vultra/core/builtin/builtin_resources.hpp"
#include "vultra/function/asset/builtin_resource_ids.hpp"

#include <vasset/vpk.hpp>

#include <cstring>
#include <memory>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__ANDROID__) || defined(__EMSCRIPTEN__)
// No single-exe embed on Android/WASM; those platforms use their own asset/VPK mechanisms.
#else
extern "C" const std::byte vultra_builtin_pack_start[];
extern "C" const std::byte vultra_builtin_pack_end[];
#endif

namespace vultra
{
    void mountBuiltinPack()
    {
        std::vector<std::byte> blob;

#if defined(__ANDROID__) || defined(__EMSCRIPTEN__)
        return; // builtin resources come from the platform asset/VPK path
#elif defined(_WIN32)
        HMODULE module = ::GetModuleHandleW(nullptr);
        HRSRC   res =
            ::FindResourceW(module, MAKEINTRESOURCEW(kBuiltinResourcePack), MAKEINTRESOURCEW(10)); // RT_RCDATA
        if (!res)
            return;
        HGLOBAL     loaded = ::LoadResource(module, res);
        const auto* data   = loaded ? static_cast<const std::byte*>(::LockResource(loaded)) : nullptr;
        const DWORD size   = ::SizeofResource(module, res);
        if (!data || size == 0)
            return;
        blob.resize(size);
        std::memcpy(blob.data(), data, size);
#else
        const std::byte* begin = vultra_builtin_pack_start;
        const std::byte* end   = vultra_builtin_pack_end;
        if (!begin || !end || end <= begin)
            return;
        blob.resize(static_cast<size_t>(end - begin));
        std::memcpy(blob.data(), begin, blob.size());
#endif

        auto fs = std::make_shared<vasset::VpkFileSystem>(std::move(blob));
        if (fs->openPackage())
        {
            // Hand the provider the full entry list so builtin::list() can enumerate.
            std::vector<std::string> paths;
            const auto&              vpk = fs->getVpk();
            paths.reserve(vpk.entries.size());
            for (const auto& e : vpk.entries)
            {
                if (static_cast<size_t>(e.pathOffset) + e.pathSize <= vpk.stringTable.size())
                    paths.emplace_back(vpk.stringTable.data() + e.pathOffset, e.pathSize);
            }
            builtin::setSource(std::move(fs), std::move(paths));
        }
    }
} // namespace vultra

namespace
{
    // Self-register at startup. This file is a direct source of each self-contained binary
    // (added by the vultra.builtin_pack rule), so the initializer always runs -- before any
    // engine subsystem (ShaderSystem/ImGuiSystem) initializes -- with no main() edit needed.
    const bool g_BuiltinPackMounted = [] {
        ::vultra::mountBuiltinPack();
        return true;
    }();
} // namespace
