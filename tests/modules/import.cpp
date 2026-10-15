#include <vultra/assets/asset_pipeline.hpp>
#include <vultra/assets/asset_source.hpp>
#include <vultra/assets/vpk_archive.hpp>
#include <vultra/platform/os/file.hpp>

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
} // namespace

int main(int argc, char** argv)
try
{
    using namespace vultra;
    require(argc == 2, "Pass an unused import directory");
    const std::filesystem::path directory(argv[1]);
    require(!std::filesystem::exists(directory), "Import directory already exists");
    const auto root = directory / "source";
    std::filesystem::create_directories(root);
    constexpr std::string_view obj = "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    writeFileAtomically(root / "triangle.obj", std::as_bytes(std::span(obj)));
    AssetImportOptions options;
    options.cacheDirectory       = directory / "cache";
    options.textures.compression = TextureCompression::eNone;
    const auto imported          = importAsset(root / "triangle.obj", options);
    require(imported.scene.vertices.size() == 3 && imported.scene.indices.size() == 3 && !imported.cacheHit,
            "CPU import did not produce its geometry");
    require(importAsset(root / "triangle.obj", options).cacheHit, "CPU import did not reuse its cache");
    const std::array files {VpkFile {"models/triangle.obj", root / "triangle.obj"}};
    VpkArchive::pack(files, directory / "model.vpk");
    std::filesystem::remove_all(root);
    AssetSource packaged {VpkArchive(directory / "model.vpk")};
    options.cacheDirectory = directory / "package-cache";
    const auto archived    = importAsset(packaged.resolve("models/triangle.obj"), options, &packaged);
    require(archived.scene.indices == imported.scene.indices &&
                archived.scene.vertices.size() == imported.scene.vertices.size(),
            "Direct VPK import changed its geometry");
    for (size_t index = 0; index < imported.scene.vertices.size(); ++index)
    {
        const auto& expected = imported.scene.vertices[index];
        const auto& actual   = archived.scene.vertices[index];
        require(actual.position == expected.position && actual.normal == expected.normal && actual.uv == expected.uv &&
                    actual.tangent == expected.tangent,
                "Direct VPK import changed vertex data");
    }
    std::cout << "CPU import passed: geometry, cache reuse and direct VPK parity\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
