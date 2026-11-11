#include "../support/check.hpp"

#include <vultra/assets/asset_source.hpp>
#include <vultra/assets/source_file.hpp>
#include <vultra/assets/vpk_archive.hpp>
#include <vultra/platform/os/file.hpp>
#include <vultra/platform/os/process.hpp>

#include <algorithm>
#include <array>
#include <iostream>
#include <span>
#include <string_view>

namespace
{
    constexpr std::string_view kAccepted = "Asset boundary accepted invalid input";
} // namespace

int main(int argc, char** argv)
{
    using namespace vultra;
    using support::must;
    using support::require;
    require(argc == 2, "Pass an unused output directory");
    const std::filesystem::path directory(argv[1]);
    require(!std::filesystem::exists(directory), "Asset output directory already exists");
    std::filesystem::create_directories(directory / "source/ui");
    constexpr std::string_view payload  = "CPU-only package bytes\n";
    const auto                 expected = std::as_bytes(std::span(payload));
    must(writeFileAtomically(directory / "source/ui/config", expected));
    must(writeFileAtomically(directory / "source/project.vproject", expected));
    const std::array files {VpkFile {"ui/config", directory / "source/ui/config"},
                            VpkFile {"project.vproject", directory / "source/project.vproject"},
                            VpkFile {"ui/config", directory / "source/ui/config"}};
    const auto       package = directory / "project.vpk";
    must(VpkArchive::pack(files, package));
    const auto archive = must(VpkArchive::create(package));
    require(must(archive.size("ui/config")) == expected.size(), "VPK entry size changed");
    const auto data = must(archive.read("ui/config"));
    require(std::ranges::equal(data, expected), "VPK writer/reader changed file bytes");
    support::rejects(VpkArchive::pack(files, package), kAccepted);
    support::rejects(archive.read("../ui/config"), kAccepted);
    support::rejects(archive.read("missing"), kAccepted);
    const std::array invalid {VpkFile {"../config", directory / "source/ui/config"}};
    support::rejects(VpkArchive::pack(invalid, directory / "invalid.vpk"), kAccepted);
    require(!std::filesystem::exists(directory / "invalid.vpk"), "Invalid package left output behind");
    const std::array conflict {VpkFile {"same", directory / "source/ui/config"},
                               VpkFile {"same", directory / "source/project.vproject"}};
    support::rejects(VpkArchive::pack(conflict, directory / "conflict.vpk"), kAccepted);

    const auto  authoredSource = must(AssetSource::create(directory / "source"));
    const auto& authored       = *authoredSource;
    require(must(authored.read("ui/config")) == data, "Filesystem/VPK source bytes differ");
    support::rejects(authored.resolve("../project.vpk"), kAccepted);
    std::filesystem::path  borrowed;
    std::vector<std::byte> owned;
    {
        AssetSource packaged {must(VpkArchive::create(package))};
        owned    = must(readSourceFile(must(packaged.resolve("ui/config")), {}, &packaged));
        borrowed = must(packaged.materialize("ui/config"));
        require(must(readSourceFile(borrowed)) == owned, "Selective materialization changed file bytes");
        require(!std::filesystem::exists(borrowed.parent_path().parent_path() / "project.vproject"),
                "Selective materialization extracted unrelated files");
    }
    require(owned == data && !std::filesystem::exists(borrowed), "Asset bytes or materialized-file lifetime is wrong");
    must(archive.extractTo(directory / "extracted"));
    require(must(readSourceFile(directory / "extracted/ui/config")) == data, "Archive extraction changed bytes");

    auto       damaged = must(readSourceFile(package));
    const auto found   = std::search(damaged.begin(), damaged.end(), expected.begin(), expected.end());
    require(found != damaged.end(), "Packed payload is missing");
    *found ^= std::byte {1};
    must(writeFileAtomically(directory / "damaged.vpk", damaged));
    const auto corrupt = must(VpkArchive::create(directory / "damaged.vpk"));
    // Entry data is verified when read, independently of the archive's valid index.
    support::rejects(corrupt.read("project.vproject"), kAccepted);

    const auto executable = must(executablePath());
    const auto embedded   = directory / executable.filename();
    must(VpkArchive::embedProject(executable, package, embedded));
    const auto appended = must(VpkArchive::embeddedProject(embedded));
    require(appended && must(appended->read("ui/config")) == data, "Embedded package changed file bytes");
    require(std::filesystem::status(executable).permissions() == std::filesystem::status(embedded).permissions(),
            "Embedding lost executable permissions");
    support::rejects(VpkArchive::embedProject(embedded, package, directory / "twice"), kAccepted);
    std::cout << "CPU asset source passed: bytes, paths, checksums, materialization and embedded VPK\n";
    return 0;
}
