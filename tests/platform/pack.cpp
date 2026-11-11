#include "../support/check.hpp"

#include <vultra/assets/vpk_archive.hpp>
#include <vultra/platform/os/file.hpp>
#include <vultra/shader/shader_compiler.hpp>

#include <array>
#include <iostream>
#include <system_error>

int main(int argc, char** argv)
{
    using namespace vultra;
    using support::must;
    if (argc != 3)
    {
        support::fail("Usage: platform-pack tests/platform new-output-directory");
    }
    const std::filesystem::path source(argv[1]);
    const std::filesystem::path output(argv[2]);
    std::error_code             error;
    if (!std::filesystem::create_directory(output, error))
    {
        support::fail("Platform pack output must be a fresh directory: " + error.message());
    }
    auto                 compiler = must(ShaderCompiler::create(std::filesystem::path {}));
    std::vector<VpkFile> files;
    for (const auto* filename : {"triangle.slang", "multipass.slang", "compute.slang"})
    {
        for (const auto* profile : {"spirv_1_5", "wgsl"})
        {
            const bool           wgsl = std::string_view(profile) == "wgsl";
            ShaderCompileOptions options;
            options.profile    = profile;
            const auto program = must(compiler.compile(source / filename, options));
            for (const auto& shader : program.shaders)
            {
                const auto name  = shader.entry.name + (wgsl ? ".wgsl" : ".spv");
                const auto bytes = wgsl ? std::as_bytes(std::span(shader.wgsl.data(), shader.wgsl.size())) :
                                          std::as_bytes(std::span(shader.words));
                must(writeFileAtomically(output / name, bytes));
                files.push_back({name, output / name});
            }
        }
    }
    constexpr std::string_view color = "0.08 0.18 0.32 1.0";
    must(writeFileAtomically(output / "clear.txt", std::as_bytes(std::span(color))));
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
    must(writeFileAtomically(output / "checker.rgba", std::as_bytes(std::span(checker))));
    files.push_back({"checker.rgba", output / "checker.rgba"});
    must(VpkArchive::pack(files, output / "project.vpk"));
    std::cout << (output / "project.vpk") << '\n';
    return 0;
}
