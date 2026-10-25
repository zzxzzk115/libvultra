#ifdef VULTRA_WITH_SCENE_PREPARATION
#include "scene_fixture.hpp"

#include <vultra/assets/texture_import.hpp>

#include <nlohmann/json.hpp>
#endif

#ifdef VULTRA_WITH_GAME_SHADER_COMPILER
#include <vultra/assets/shader_asset.hpp>
#endif
#include <vultra/assets/vpk_archive.hpp>
#include <vultra/drivers/rhi/shader_compiler.hpp>
#include <vultra/platform/os/file.hpp>

#include <array>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv)
try
{
    using namespace vultra;
    if (argc != 3)
    {
        throw std::invalid_argument("Usage: platform-pack tests/platform new-output-directory");
    }
    const std::filesystem::path source(argv[1]);
    const std::filesystem::path output(argv[2]);
    if (!std::filesystem::create_directory(output))
    {
        throw std::invalid_argument("Platform pack output must be a fresh directory");
    }
    ShaderCompiler       compiler(std::filesystem::path {});
    std::vector<VpkFile> files;
    for (const auto* filename : {"triangle.slang", "multipass.slang", "compute.slang"})
    {
        for (const auto* profile : {"spirv_1_5", "wgsl"})
        {
            const bool           wgsl = std::string_view(profile) == "wgsl";
            ShaderCompileOptions options;
            options.profile    = profile;
            const auto program = compiler.compile(source / filename, options);
            for (const auto& shader : program.shaders)
            {
                const auto name  = shader.entry.name + (wgsl ? ".wgsl" : ".spv");
                const auto bytes = wgsl ? std::as_bytes(std::span(shader.wgsl.data(), shader.wgsl.size())) :
                                          std::as_bytes(std::span(shader.words));
                writeFileAtomically(output / name, bytes);
                files.push_back({name, output / name});
            }
        }
    }
#ifdef VULTRA_WITH_GAME_SHADER_COMPILER
    for (const auto* profile : {"spirv_1_5", "wgsl"})
    {
        ShaderCompileOptions options;
        options.profile = profile;
        const auto name = std::string("material-") + profile + ".vshaderc";
        ShaderAsset::compile(source / "material.vshader", options).save(output / name);
        files.push_back({name, output / name});
    }
#endif
#if defined(VULTRA_WITH_SCENE_PREPARATION) && defined(VULTRA_WITH_GAME_SHADER_COMPILER)
    const auto scene    = fixture::texturedQuad();
    const auto prepared = prepareTextures(scene, {.compression = TextureCompression::eNone});
    writeFileAtomically(output / "scene-vertices.bin", std::as_bytes(std::span(scene.vertices)));
    writeFileAtomically(output / "scene-indices.bin", std::as_bytes(std::span(scene.indices)));
    files.push_back({"scene-vertices.bin", output / "scene-vertices.bin"});
    files.push_back({"scene-indices.bin", output / "scene-indices.bin"});
    nlohmann::json descriptor {{"version", 1}, {"materials", prepared.materials}, {"images", nlohmann::json::array()}};
    for (uint32_t index = 0; index < prepared.images.size(); ++index)
    {
        const auto&    image  = prepared.images[index];
        nlohmann::json levels = nlohmann::json::array();
        for (uint32_t mip = 0; mip < image.levels.size(); ++mip)
        {
            const auto& level = image.levels[mip];
            const auto  name  = "scene-image-" + std::to_string(index) + "-" + std::to_string(mip) + ".bin";
            writeFileAtomically(output / name, level.bytes);
            files.push_back({name, output / name});
            levels.push_back({{"width", level.size.width}, {"height", level.size.height}, {"file", name}});
        }
        descriptor["images"].push_back({{"format", image.format}, {"levels", levels}});
    }
    const auto text = descriptor.dump();
    writeFileAtomically(output / "scene-fixture.json", std::as_bytes(std::span(text)));
    files.push_back({"scene-fixture.json", output / "scene-fixture.json"});
    for (const auto* profile : {"spirv_1_5", "wgsl"})
    {
        ShaderCompileOptions options;
        options.profile = profile;
        const auto name = std::string("scene-") + profile + ".vshaderc";
        ShaderAsset::compile(source / "scene.vshader", options).save(output / name);
        files.push_back({name, output / name});
    }
#endif
    constexpr std::string_view color = "0.08 0.18 0.32 1.0";
    writeFileAtomically(output / "clear.txt", std::as_bytes(std::span(color)));
    files.push_back({"clear.txt", output / "clear.txt"});
    std::array<uint8_t, 16 * 16 * 4> checker {};
    for (uint32_t y = 0; y < 16; ++y)
    {
        for (uint32_t x = 0; x < 16; ++x)
        {
            const uint8_t value  = ((x / 4 + y / 4) % 2 == 0) ? 230 : 95;
            const size_t  offset = (y * 16 + x) * 4;
            checker[offset]      = value;
            checker[offset + 1]  = value;
            checker[offset + 2]  = value;
            checker[offset + 3]  = 255;
        }
    }
    writeFileAtomically(output / "checker.rgba", std::as_bytes(std::span(checker)));
    files.push_back({"checker.rgba", output / "checker.rgba"});
    VpkArchive::pack(files, output / "project.vpk");
    std::cout << (output / "project.vpk") << '\n';
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
