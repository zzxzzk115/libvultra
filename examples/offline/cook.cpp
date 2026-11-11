#include <vultra/assets/vpk_archive.hpp>
#include <vultra/shader/shader_compiler.hpp>

#include <array>
#include <iostream>
#include <system_error>

namespace
{
    std::expected<std::filesystem::path, vultra::Error> cook(int argc, char** argv)
    {
        using namespace vultra;
        if (argc != 3 && argc != 4)
        {
            return makeError("Usage: example-offline-cook repository-root fresh-output-directory [spirv_1_5|wgsl]");
        }
        const std::filesystem::path root(argv[1]);
        const std::filesystem::path output(argv[2]);
        std::error_code             failure;
        const bool                  created = std::filesystem::create_directory(output, failure);
        if (failure)
        {
            return makeError("Create {}: {}", output.string(), failure.message());
        }
        if (!created)
        {
            return makeError("Use a fresh output directory");
        }
        const std::array sources {VpkFile {"mesh.vshaderc", "examples/common/colored_mesh.slang"},
                                  VpkFile {"gain.vshaderc", "examples/offline/shaders/color_gain.slang"},
                                  VpkFile {"display.vshaderc", "builtin/shaders/passes/texture_blit.slang"}};
        auto             compiler = ShaderCompiler::create(std::filesystem::path {});
        if (!compiler)
        {
            return std::unexpected(compiler.error());
        }
        ShaderCompileOptions options;
        options.includeDirectories = {root / "builtin/shaders"};
        if (argc == 4)
        {
            options.profile = argv[3];
        }
        std::vector<VpkFile> files;
        for (const auto& source : sources)
        {
            const auto file    = output / source.path;
            const auto program = compiler->compile(root / source.source, options);
            if (!program)
            {
                return std::unexpected(program.error());
            }
            if (auto saved = program->save(file); !saved)
            {
                return std::unexpected(saved.error());
            }
            files.push_back({source.path, file});
        }
        files.push_back({"color_gain.vgraph", root / "examples/offline/color_gain.vgraph"});
        if (auto packed = VpkArchive::pack(files, output / "project.vpk"); !packed)
        {
            return std::unexpected(packed.error());
        }
        return output / "project.vpk";
    }
} // namespace

int main(int argc, char** argv)
{
    const auto pack = cook(argc, argv);
    if (!pack)
    {
        std::cerr << pack.error().message << '\n';
        return 1;
    }
    std::cout << *pack << '\n';
    return 0;
}
