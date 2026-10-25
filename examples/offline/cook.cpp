#include <vultra/assets/vpk_archive.hpp>
#include <vultra/drivers/rhi/shader_compiler.hpp>

#include <array>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv)
try
{
    using namespace vultra;
    if (argc != 3 && argc != 4)
    {
        throw std::invalid_argument(
            "Usage: example-offline-cook repository-root fresh-output-directory [spirv_1_5|wgsl]");
    }
    const std::filesystem::path root(argv[1]);
    const std::filesystem::path output(argv[2]);
    if (!std::filesystem::create_directory(output))
    {
        throw std::invalid_argument("Use a fresh output directory");
    }
    const std::array     sources {VpkFile {"mesh.vshaderc", "examples/common/colored_mesh.slang"},
                                  VpkFile {"gain.vshaderc", "examples/offline/shaders/color_gain.slang"},
                                  VpkFile {"display.vshaderc", "builtin/shaders/passes/texture_blit.slang"}};
    ShaderCompiler       compiler(std::filesystem::path {});
    ShaderCompileOptions options;
    options.includeDirectories = {root / "builtin/shaders"};
    if (argc == 4)
    {
        options.profile = argv[3];
    }
    std::vector<VpkFile> files;
    for (const auto& source : sources)
    {
        const auto file = output / source.path;
        compiler.compile(root / source.source, options).save(file);
        files.push_back({source.path, file});
    }
    files.push_back({"color_gain.vgraph", root / "examples/offline/color_gain.vgraph"});
    VpkArchive::pack(files, output / "project.vpk");
    std::cout << (output / "project.vpk") << '\n';
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
