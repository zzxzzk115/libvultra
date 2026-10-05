#include <vultra/assets/project_manifest.hpp>
#include <vultra/assets/vpk_archive.hpp>
#include <vultra/scene/scene_tree.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
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
    void requireFailure(Callback&& callback, const char* message)
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
        require(rejected, message);
    }

    void overwriteByte(const std::filesystem::path& path, std::streamoff offset, char value)
    {
        std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
        file.seekp(offset);
        file.put(value);
        require(bool(file), "Could not corrupt VPK test file");
    }
} // namespace

int main()
try
{
    namespace fs = std::filesystem;
    const auto root =
        fs::path("build/.tmp/vpk") / std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto output = root / "research.vpk";
    fs::create_directories(root);
    vultra::VpkArchive::packProject("resources/research.vproject", output);
    const auto packedSize = fs::file_size(output);
    require(packedSize > 5'000'000, "VPK omitted model or environment data");
    requireFailure(
        [&]
        {
            vultra::VpkArchive::packProject("resources/research.vproject", output);
        },
        "Packing overwrote an existing VPK");
    require(fs::file_size(output) == packedSize, "Rejected VPK output was modified");

    vultra::VpkArchive archive(output);
    require(archive.contains("project.vproject") && archive.contains("models/DamagedHelmet/DamagedHelmet.glb") &&
                archive.contains("textures/environment_maps/citrus_orchard_puresky_1k.hdr") &&
                archive.contains("ui/hud.rml") && archive.contains("ui/hud.rcss") &&
                archive.contains("ui/LatoLatin-Regular.ttf") && archive.contains("ui/LICENSE.txt") &&
                !archive.contains("builtin/shaders/passes/forward.slang"),
            "Project VPK content is incorrect");
    const auto managedRoot = root / "managed";
    fs::create_directories(managedRoot / "scripts");
    vultra::SceneTree managedScene(std::make_unique<vultra::Node>("Root"));
    managedScene.save(managedRoot / "main.vscene");
    vultra::ProjectManifest managedProject;
    managedProject.mainScene = "main.vscene";
    managedProject.extensions.push_back("scripts/Extension.so");
    managedProject.scripts.push_back({vultra::ScriptModule::Language::eNative,
                                      "scripts/Extension.so",
                                      "ShipOrbit",
                                      managedScene.root().idInScene().value});
    managedProject.scripts.push_back({vultra::ScriptModule::Language::eCSharp,
                                      "scripts/Game.dll",
                                      "Game.Controller",
                                      managedScene.root().idInScene().value});
    managedProject.save(managedRoot / "project.vproject");
    const auto restoredManaged = vultra::ProjectManifest::load(managedRoot / "project.vproject");
    require(restoredManaged.extensions.size() == 1 && restoredManaged.extensions.front() == "scripts/Extension.so" &&
                restoredManaged.scripts.size() == 2 && restoredManaged.scripts.front().typeName == "ShipOrbit" &&
                restoredManaged.scripts.front().path == restoredManaged.extensions.front() &&
                restoredManaged.scripts.back().typeName == "Game.Controller" &&
                restoredManaged.scripts.back().node == managedScene.root().idInScene().value,
            "Extension class or managed script binding was not preserved");
    auto missingClass = managedProject;
    missingClass.scripts.back().typeName.clear();
    requireFailure(
        [&]
        {
            missingClass.save(managedRoot / "invalid.vproject");
        },
        "Managed script without a class name was accepted");
    {
        std::ofstream(managedRoot / "scripts/Extension.so") << "native extension";
        std::ofstream(managedRoot / "scripts/Game.dll") << "managed assembly";
        std::ofstream(managedRoot / "scripts/Vultra.ManagedHost.dll") << "host assembly";
        std::ofstream(managedRoot / "scripts/Vultra.Scripting.dll") << "scripting API assembly";
        std::ofstream(managedRoot / "scripts/Vultra.ManagedHost.runtimeconfig.json") << "{}";
        std::ofstream(managedRoot / "scripts/Vultra.ManagedHost.deps.json") << "{}";
        std::ofstream(managedRoot / "scripts/Game.deps.json") << "{}";
    }
    const auto managedPack = root / "managed.vpk";
    vultra::VpkArchive::packProject(managedRoot / "project.vproject", managedPack);
    vultra::VpkArchive managedArchive(managedPack);
    require(managedArchive.contains("scripts/Extension.so") && managedArchive.contains("scripts/Game.dll") &&
                managedArchive.contains("scripts/Vultra.ManagedHost.dll") &&
                managedArchive.contains("scripts/Vultra.Scripting.dll") &&
                managedArchive.contains("scripts/Vultra.ManagedHost.runtimeconfig.json") &&
                managedArchive.contains("scripts/Vultra.ManagedHost.deps.json") &&
                managedArchive.contains("scripts/Game.deps.json"),
            "Managed VPK omitted assembly sidecars");
    const auto scriptingApi = managedRoot / "scripts/Vultra.Scripting.dll";
    fs::rename(scriptingApi, managedRoot / "scripts/Vultra.Scripting.saved");
    requireFailure(
        [&]
        {
            vultra::VpkArchive::packProject(managedRoot / "project.vproject", root / "missing-scripting-api.vpk");
        },
        "Managed VPK accepted a missing scripting API assembly");
    fs::rename(managedRoot / "scripts/Vultra.Scripting.saved", scriptingApi);
    fs::remove(managedRoot / "scripts/Vultra.ManagedHost.runtimeconfig.json");
    requireFailure(
        [&]
        {
            vultra::VpkArchive::packProject(managedRoot / "project.vproject", root / "missing-managed-config.vpk");
        },
        "Managed VPK accepted missing runtime configuration");

    const auto builtinOutput = root / "builtin.vpk";
    vultra::VpkArchive::packBuiltins(".", builtinOutput);
    vultra::VpkArchive builtins(builtinOutput);
    require(builtins.contains("builtin/shaders/passes/forward.vshaderc") &&
                builtins.contains("builtin/shaders/passes/path_trace.vshaderc") &&
                builtins.contains("builtin/shaders/passes/meshlet_forward.vshaderc") &&
                !builtins.contains("builtin/shaders/passes/forward.slang") &&
                !builtins.contains("external/openpbr/openpbr.h") && builtins.contains("external/openpbr/LICENSE") &&
                !builtins.contains("project.vproject"),
            "Builtin VPK content is incorrect");

    const auto stub = root / "runtime-stub";
    {
        std::ofstream file(stub, std::ios::binary);
        file << "Vultra executable stub";
    }
    fs::permissions(stub, fs::perms::owner_exec, fs::perm_options::add);
    require(!vultra::VpkArchive::embeddedProject(stub), "Unpacked executable reported an embedded project");
    const auto single = root / "single-runtime";
    vultra::VpkArchive::embedProject(stub, output, single);
    require(fs::file_size(single) == fs::file_size(stub) + packedSize + 32, "Embedded executable size is incorrect");
    require((fs::status(single).permissions() & fs::perms::owner_exec) != fs::perms::none,
            "Embedded executable lost its execute permission");
    const auto readOnlyStub = root / "read-only-stub";
    fs::copy_file(stub, readOnlyStub);
    fs::permissions(readOnlyStub, fs::perms::owner_write, fs::perm_options::remove);
    const auto readOnlySingle = root / "read-only-single";
    vultra::VpkArchive::embedProject(readOnlyStub, output, readOnlySingle);
    require(fs::status(readOnlySingle).permissions() == fs::status(readOnlyStub).permissions(),
            "Embedding did not preserve read-only executable permissions");
    auto embedded = vultra::VpkArchive::embeddedProject(single);
    require(embedded && embedded->contains("project.vproject") &&
                embedded->read("project.vproject") == archive.read("project.vproject"),
            "Embedded VPK cannot read the project manifest");
    requireFailure(
        [&]
        {
            vultra::VpkArchive::embedProject(stub, output, single);
        },
        "Embedding overwrote an existing executable");
    requireFailure(
        [&]
        {
            vultra::VpkArchive::embedProject(single, output, root / "nested-runtime");
        },
        "Embedding accepted an already-packed executable");
    requireFailure(
        [&]
        {
            vultra::VpkArchive::embedProject(stub, builtinOutput, root / "builtin-runtime");
        },
        "Embedding accepted a VPK without a project manifest");
    const auto badEmbedded = root / "bad-embedded-runtime";
    fs::copy_file(single, badEmbedded);
    overwriteByte(badEmbedded, std::streamoff(fs::file_size(stub)), 0);
    requireFailure(
        [&]
        {
            vultra::VpkArchive::embeddedProject(badEmbedded);
        },
        "Embedded VPK checksum did not reject altered payload");
    const auto badFooter = root / "bad-footer-runtime";
    fs::copy_file(single, badFooter);
    overwriteByte(badFooter, std::streamoff(fs::file_size(badFooter) - 24), 2);
    requireFailure(
        [&]
        {
            vultra::VpkArchive::embeddedProject(badFooter);
        },
        "Embedded VPK accepted an unknown footer version");
    requireFailure(
        [&]
        {
            archive.read("../escape");
        },
        "VPK accepted path traversal");
    const auto extracted = root / "extracted";
    archive.extractTo(extracted);
    requireFailure(
        [&]
        {
            archive.extractTo(extracted);
        },
        "VPK overwrote an existing extraction");
    auto project = vultra::ProjectManifest::load(extracted / "project.vproject");
    auto scene   = vultra::SceneTree::load(extracted / project.mainScene);
    scene.validateAssets(project);
    require(project.environment && fs::file_size(extracted / project.asset(*project.environment).path) > 1'000'000,
            "Environment asset was not extracted");
    require(fs::file_size(extracted / "models/DamagedHelmet/DamagedHelmet.glb") ==
                fs::file_size("resources/models/DamagedHelmet/DamagedHelmet.glb"),
            "Model asset size changed in VPK");

    const auto corrupt = root / "corrupt.vpk";
    fs::copy_file(output, corrupt);
    overwriteByte(corrupt, 24, 0);
    vultra::VpkArchive corrupted(corrupt);
    requireFailure(
        [&]
        {
            corrupted.read("models/DamagedHelmet/DamagedHelmet.glb");
        },
        "VPK checksum did not reject altered payload");
    const auto oldVersion = root / "old-version.vpk";
    fs::copy_file(output, oldVersion);
    overwriteByte(oldVersion, 8, 2);
    requireFailure(
        [&]
        {
            vultra::VpkArchive invalid(oldVersion);
        },
        "VPK accepted an unknown version");
    return 0;
}
catch (const std::exception& error)
{
    fprintf(stderr, "VPK test: %s\n", error.what());
    return 1;
}
