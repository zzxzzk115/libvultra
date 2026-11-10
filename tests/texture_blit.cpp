#include "support/texture_blit_probe.hpp"

#include <cstdio>
#include <fstream>

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::fprintf(stderr, "Usage: test-texture-blit texture_blit.vshaderc\n");
        return 2;
    }
    auto          device = vultra::Device::create(true);
    std::ifstream file(argv[1], std::ios::binary | std::ios::ate);
    if (!device || !file || file.tellg() <= 0)
    {
        return 1;
    }
    std::vector<std::byte> bytes(static_cast<size_t>(file.tellg()));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size())))
    {
        return 1;
    }
    auto program = vultra::ShaderProgram::loadExpected(bytes, argv[1]);
    if (!program)
    {
        std::fprintf(stderr, "%s\n", program.error().c_str());
        return 1;
    }
    if (auto result = vultra::testing::textureBlitProbe(**device, *program); !result)
    {
        std::fprintf(stderr, "%s\n", result.error().c_str());
        return 1;
    }
    std::puts("Filtered blit preserves HDR/alpha, clamp addressing and source slots across sampler failure");
    return 0;
}
