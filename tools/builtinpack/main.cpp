// builtinpack: build-time host tool that packs libvultra's builtin engine resources
// (shaders, fonts, textures, render graphs) into a single zstd-compressed VPK. The pack
// is then embedded into the executable (.rc on Windows, linker symbols elsewhere) and
// mounted into the VFS at startup, replacing the giant compiled-in C arrays.
//
// Usage:
//   builtinpack <out.vpk> <logicalPath> <sourceFile> [<logicalPath> <sourceFile> ...]
//
// Each (logicalPath, sourceFile) pair adds one entry; logicalPath is what the engine
// reads via the VFS (e.g. "shaders/builtin_highend.vshlib"). Missing source files are
// skipped with a warning so an optional resource (e.g. the CJK font) can be omitted.

#include <vasset/vpk.hpp>

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace
{
    bool readFile(const std::string& path, std::vector<std::byte>& out)
    {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f)
            return false;
        const std::streamsize size = f.tellg();
        if (size < 0)
            return false;
        f.seekg(0, std::ios::beg);
        out.resize(static_cast<size_t>(size));
        if (size > 0 && !f.read(reinterpret_cast<char*>(out.data()), size))
            return false;
        return true;
    }
} // namespace

int main(int argc, char** argv)
{
    if (argc < 2 || (argc % 2) != 0)
    {
        std::fprintf(stderr, "usage: builtinpack <out.vpk> [<logicalPath> <sourceFile>]...\n");
        return 2;
    }

    const std::string outPath = argv[1];

    std::vector<vasset::VpkWriteItem> items;
    size_t                            packed = 0;
    size_t                            missing = 0;

    for (int i = 2; i + 1 < argc; i += 2)
    {
        const std::string logical = argv[i];
        const std::string source  = argv[i + 1];

        std::vector<std::byte> bytes;
        if (!readFile(source, bytes))
        {
            std::fprintf(stderr, "[builtinpack] skip (missing): %s <- %s\n", logical.c_str(), source.c_str());
            ++missing;
            continue;
        }

        vasset::VpkWriteItem item;
        item.logicalPath  = logical;
        item.bytes        = std::move(bytes);
        item.allowCompress = true; // writeVpk skips zstd for already-compressed formats by extension
        items.push_back(std::move(item));
        ++packed;
    }

    // zstd level 19: build-time cost is irrelevant; we want the smallest embedded blob.
    auto r = vasset::writeVpk(outPath, items, 19);
    if (!r)
    {
        std::fprintf(stderr, "[builtinpack] writeVpk failed for %s\n", outPath.c_str());
        return 1;
    }

    std::printf("[builtinpack] wrote %s (%zu entries, %zu missing)\n", outPath.c_str(), packed, missing);
    return 0;
}
