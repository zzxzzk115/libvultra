// Per-binary accessor for the builtin.vpk. Compiled into each self-contained binary (editor,
// runtime, examples) by the `vultra.builtin_pack` rule. It obtains the pack bytes in a
// platform-specific way and installs them as the builtin:: resource source:
//   - Windows: .rc RCDATA resource embedded in the binary;
//   - Linux/macOS: .incbin symbols embedded in the binary;
//   - wasm: /builtin.vpk baked into MEMFS by the build (--embed-file / --preload-file);
//   - Android: nothing here -- the runtime entry reads the pack from the APK once the asset
//     manager / internal storage is available and calls mountBuiltinPackFromBytes().

#include "vultra/core/builtin/builtin_resources.hpp"
#include "vultra/function/asset/builtin_resource_ids.hpp"

#include <vasset/vpk.hpp>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__EMSCRIPTEN__)
#include <fstream>
#elif defined(__ANDROID__)
// No binary embed on Android; the pack is read from the APK by the runtime entry.
#else
extern "C" const std::byte vultra_builtin_pack_start[];
extern "C" const std::byte vultra_builtin_pack_end[];
#endif

namespace vultra
{
    void mountBuiltinPackFromBytes(std::vector<std::byte> blob)
    {
        if (blob.empty())
            return;

        auto fs = std::make_shared<vasset::VpkFileSystem>(std::move(blob));
        if (!fs->openPackage())
            return;

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

    void mountBuiltinPack()
    {
        std::vector<std::byte> blob;

#if defined(__ANDROID__)
        return; // builtin resources are mounted explicitly by the Android runtime entry
#elif defined(__EMSCRIPTEN__)
        // The build bakes builtin.vpk into MEMFS at /builtin.vpk. Emscripten resolves preloaded
        // data (run-dependencies) before global constructors run, so this static-init read is safe.
        std::ifstream in("/builtin.vpk", std::ios::binary | std::ios::ate);
        if (!in)
            return;
        const std::streamsize size = in.tellg();
        if (size <= 0)
            return;
        in.seekg(0);
        blob.resize(static_cast<size_t>(size));
        if (!in.read(reinterpret_cast<char*>(blob.data()), size))
            return;
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

        mountBuiltinPackFromBytes(std::move(blob));
    }
} // namespace vultra

namespace
{
    // Self-register at startup. This file is a direct source of each self-contained binary
    // (added by the vultra.builtin_pack rule), so the initializer always runs -- before any
    // engine subsystem (ShaderSystem/ImGuiSystem) initializes -- with no main() edit needed.
    // On Android this resolves to a no-op; that platform mounts the pack from its runtime entry.
    const bool g_BuiltinPackMounted = [] {
        ::vultra::mountBuiltinPack();
        return true;
    }();
} // namespace
