#include <vultra/assets/asset_source.hpp>
#include <vultra/assets/source_file.hpp>
#include <vultra/assets/vpk_archive.hpp>
#ifdef TEST_PROJECT
#include <vultra/assets/project_manifest.hpp>
#endif
#include <vultra/platform/os/file.hpp>
#include <vultra/platform/os/process.hpp>

#include <algorithm>
#include <array>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string_view>

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
        bool rejected = false;
        try
        {
            callback();
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        require(rejected, "Asset boundary accepted invalid input");
    }
} // namespace

int main(int argc, char** argv)
try
{
    using namespace vultra;
    require(argc == 2, "Pass an unused output directory");
    const std::filesystem::path directory(argv[1]);
    require(!std::filesystem::exists(directory), "Asset output directory already exists");
    std::filesystem::create_directories(directory / "source/ui");
    constexpr std::string_view payload  = "CPU-only package bytes\n";
    const auto                 expected = std::as_bytes(std::span(payload));
    writeFileAtomically(directory / "source/ui/config", expected);
    writeFileAtomically(directory / "source/project.vproject", expected);
    const std::array files {VpkFile {"ui/config", directory / "source/ui/config"},
                            VpkFile {"project.vproject", directory / "source/project.vproject"},
                            VpkFile {"ui/config", directory / "source/ui/config"}};
    const auto       package = directory / "project.vpk";
    VpkArchive::pack(files, package);
    VpkArchive archive(package);
    require(archive.size("ui/config") == expected.size(), "VPK entry size changed");
    const auto data = archive.read("ui/config");
    require(std::ranges::equal(data, expected), "VPK writer/reader changed file bytes");
    reject(
        [&]
        {
            VpkArchive::pack(files, package);
        });
    reject(
        [&]
        {
            archive.read("../ui/config");
        });
    reject(
        [&]
        {
            archive.read("missing");
        });
    const std::array invalid {VpkFile {"../config", directory / "source/ui/config"}};
    reject(
        [&]
        {
            VpkArchive::pack(invalid, directory / "invalid.vpk");
        });
    require(!std::filesystem::exists(directory / "invalid.vpk"), "Invalid package left output behind");
    const std::array conflict {VpkFile {"same", directory / "source/ui/config"},
                               VpkFile {"same", directory / "source/project.vproject"}};
    reject(
        [&]
        {
            VpkArchive::pack(conflict, directory / "conflict.vpk");
        });

    AssetSource authored(directory / "source");
    require(authored.read("ui/config") == data, "Filesystem/VPK source bytes differ");
    reject(
        [&]
        {
            authored.resolve("../project.vpk");
        });
    std::filesystem::path  borrowed;
    std::vector<std::byte> owned;
    {
        AssetSource packaged {VpkArchive(package)};
        owned    = readSourceFile(packaged.resolve("ui/config"), {}, &packaged);
        borrowed = packaged.materialize("ui/config");
        require(readSourceFile(borrowed) == owned, "Selective materialization changed file bytes");
        require(!std::filesystem::exists(borrowed.parent_path().parent_path() / "project.vproject"),
                "Selective materialization extracted unrelated files");
    }
    require(owned == data && !std::filesystem::exists(borrowed), "Asset bytes or materialized-file lifetime is wrong");
    archive.extractTo(directory / "extracted");
    require(readSourceFile(directory / "extracted/ui/config") == data, "Archive extraction changed bytes");

    auto       damaged = readSourceFile(package);
    const auto found   = std::search(damaged.begin(), damaged.end(), expected.begin(), expected.end());
    require(found != damaged.end(), "Packed payload is missing");
    *found ^= std::byte {1};
    writeFileAtomically(directory / "damaged.vpk", damaged);
    VpkArchive corrupt(directory / "damaged.vpk");
    // Entry data is verified when read, independently of the archive's valid index.
    reject(
        [&]
        {
            corrupt.read("project.vproject");
        });

    const auto executable = executablePath();
    const auto embedded   = directory / executable.filename();
    VpkArchive::embedProject(executable, package, embedded);
    const auto appended = VpkArchive::embeddedProject(embedded);
    require(appended && appended->read("ui/config") == data, "Embedded package changed file bytes");
    require(std::filesystem::status(executable).permissions() == std::filesystem::status(embedded).permissions(),
            "Embedding lost executable permissions");
    reject(
        [&]
        {
            VpkArchive::embedProject(embedded, package, directory / "twice");
        });
#ifdef TEST_PROJECT
    ProjectManifest project;
    project.mainScene = "main.vscene";
    project.metadata  = R"({"seed":17})";
    const auto id     = project.addAsset("data/source");
    project.save(directory / "config.vproject");
    const auto restored = ProjectManifest::load(directory / "config.vproject");
    require(restored.asset(id).path == "data/source" && restored.metadata == project.metadata,
            "CPU project persistence changed its asset identity or metadata");
    project.renameAsset(id, "data/renamed");
    project.save(directory / "renamed.vproject");
    require(ProjectManifest::load(directory / "renamed.vproject").asset(id).path == "data/renamed",
            "CPU project rename lost persistent asset identity");
    project.metadata = "[]";
    reject(
        [&]
        {
            project.save(directory / "invalid.vproject");
        });
    require(!std::filesystem::exists(directory / "invalid.vproject"), "Invalid project left output behind");
#endif
    std::cout << "CPU asset source passed: bytes, paths, checksums, materialization and embedded VPK\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
