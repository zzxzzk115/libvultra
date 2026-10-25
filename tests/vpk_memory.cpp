#include <vultra/assets/asset_source.hpp>
#include <vultra/assets/source_file.hpp>
#include <vultra/assets/vpk_archive.hpp>
#include <vultra/core/base/stable_id.hpp>
#include <vultra/platform/os/file.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    template<typename Callback>
    void reject(Callback&& callback)
    {
        try
        {
            callback();
        }
        catch (const std::exception&)
        {
            return;
        }
        throw std::runtime_error("Embedded VPK accepted invalid input");
    }
} // namespace

int main()
try
{
    using namespace vultra;
    require(VpkArchive::canonicalPath("textures/albedo.png") == "textures/albedo.png",
            "VPK entry validation changed a canonical path");
    for (const auto* path : {"", "../asset", "/asset", "folder/../asset", "folder\\asset", "./asset", "folder//asset"})
    {
        reject(
            [&]
            {
                VpkArchive::canonicalPath(path);
            });
    }
    const auto root = std::filesystem::temp_directory_path() / ("vpk-memory-" + StableId::generate().toString());
    std::filesystem::create_directory(root);
    constexpr std::string_view text = "embedded bytes, including UTF-8: \xe4\xbd\xa0\xe5\xa5\xbd";
    writeFileAtomically(root / "payload", std::as_bytes(std::span(text)));
    writeFileAtomically(root / "empty", {});
    const std::array files {VpkFile {"ui/data", root / "payload"}, VpkFile {"empty", root / "empty"}};
    VpkArchive::pack(files, root / "project.vpk");
    const auto bytes    = readSourceFile(root / "project.vpk");
    const auto expected = VpkArchive(root / "project.vpk").read("ui/data");
    std::filesystem::remove_all(root);

    // The physical archive and source files no longer exist; identity is only a namespace.
    VpkArchive  archive(root / "project.vpk", bytes);
    AssetSource source(archive);
    require(archive.contains("ui/data") && !archive.contains("missing") && archive.size("empty") == 0 &&
                archive.read("empty").empty() && source.read("ui/data") == expected &&
                source.read(source.resolve("ui/data")) == expected && !std::filesystem::exists(source.root()),
            "Memory/file VPK parity or virtual path resolution failed");
    reject(
        [&]
        {
            source.read("../payload");
        });
    reject(
        [&]
        {
            source.read("missing");
        });
    const auto owned = source.read("ui/data");
    require(std::ranges::equal(owned, std::as_bytes(std::span(text))), "Embedded entry bytes changed");

    for (size_t length : {size_t(0), size_t(23), bytes.size() - 1})
    {
        reject(
            [&]
            {
                VpkArchive invalid(root / "truncated", std::span(bytes).first(length));
            });
    }
    auto damaged = bytes;
    damaged[24] ^= std::byte {1};
    {
        VpkArchive corrupt(root / "corrupt", damaged);
        reject(
            [&]
            {
                corrupt.read("ui/data");
            });
    }
    damaged    = bytes;
    damaged[8] = std::byte {2};
    reject(
        [&]
        {
            VpkArchive invalid(root / "version", damaged);
        });
    damaged = bytes;
    std::fill(damaged.begin() + 16, damaged.begin() + 24, std::byte {0xff});
    reject(
        [&]
        {
            VpkArchive invalid(root / "index", damaged);
        });
    std::cout << "Embedded VPK passed: file parity, no extraction, owned reads, bounds and checksums\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
