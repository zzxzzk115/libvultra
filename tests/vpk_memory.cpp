#include "support/check.hpp"

#include <vultra/assets/asset_source.hpp>
#include <vultra/assets/source_file.hpp>
#include <vultra/assets/vpk_archive.hpp>
#include <vultra/core/base/stable_id.hpp>
#include <vultra/platform/os/file.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>

int main()
{
    using namespace vultra;
    using support::must;
    using support::require;
    constexpr std::string_view rejected = "Embedded VPK accepted invalid input";
    require(must(VpkArchive::canonicalPath("textures/albedo.png")) == "textures/albedo.png",
            "VPK entry validation changed a canonical path");
    for (const auto* path : {"", "../asset", "/asset", "folder/../asset", "folder\\asset", "./asset", "folder//asset"})
    {
        support::rejects(VpkArchive::canonicalPath(path), rejected);
    }
    const auto root = std::filesystem::temp_directory_path() / ("vpk-memory-" + StableId::generate().toString());
    std::filesystem::create_directory(root);
    constexpr std::string_view text = "embedded bytes, including UTF-8: \xe4\xbd\xa0\xe5\xa5\xbd";
    must(writeFileAtomically(root / "payload", std::as_bytes(std::span(text))));
    must(writeFileAtomically(root / "empty", {}));
    const std::array files {VpkFile {"ui/data", root / "payload"}, VpkFile {"empty", root / "empty"}};
    must(VpkArchive::pack(files, root / "project.vpk"));
    const auto bytes    = must(readSourceFile(root / "project.vpk"));
    const auto expected = must(must(VpkArchive::create(root / "project.vpk")).read("ui/data"));
    std::filesystem::remove_all(root);

    // The physical archive and source files no longer exist; identity is only a namespace.
    auto        archive = must(VpkArchive::create(root / "project.vpk", bytes));
    AssetSource source(archive);
    require(
        must(archive.contains("ui/data")) && !must(archive.contains("missing")) && must(archive.size("empty")) == 0 &&
            must(archive.read("empty")).empty() && must(source.read("ui/data")) == expected &&
            must(source.read(must(source.resolve("ui/data")))) == expected && !std::filesystem::exists(source.root()),
        "Memory/file VPK parity or virtual path resolution failed");
    support::rejects(source.read("../payload"), rejected);
    support::rejects(source.read("missing"), rejected);
    const auto owned = must(source.read("ui/data"));
    require(std::ranges::equal(owned, std::as_bytes(std::span(text))), "Embedded entry bytes changed");

    for (size_t length : {size_t(0), size_t(23), bytes.size() - 1})
    {
        support::rejects(VpkArchive::create(root / "truncated", std::span(bytes).first(length)), rejected);
    }
    auto damaged = bytes;
    damaged[24] ^= std::byte {1};
    {
        const auto corrupt = must(VpkArchive::create(root / "corrupt", damaged));
        support::rejects(corrupt.read("ui/data"), rejected);
    }
    damaged    = bytes;
    damaged[8] = std::byte {2};
    support::rejects(VpkArchive::create(root / "version", damaged), rejected);
    damaged = bytes;
    std::fill(damaged.begin() + 16, damaged.begin() + 24, std::byte {0xff});
    support::rejects(VpkArchive::create(root / "index", damaged), rejected);
    std::cout << "Embedded VPK passed: file parity, no extraction, owned reads, bounds and checksums\n";
    return 0;
}
