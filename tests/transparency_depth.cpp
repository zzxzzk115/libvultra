#include "support/transparency_depth_probe.hpp"

#include <cstdio>
#include <fstream>
#include <vector>

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::fprintf(stderr, "Usage: test-transparency-depth shader.vshaderc\n");
        return 2;
    }
    std::ifstream input(argv[1], std::ios::binary | std::ios::ate);
    if (!input || input.tellg() <= 0)
        return 1;
    std::vector<std::byte> bytes(static_cast<size_t>(input.tellg()));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size())))
        return 1;
    auto shader = vultra::ShaderProgram::loadExpected(bytes, argv[1]);
    auto device = vultra::Device::create(true);
    if (!shader || !device)
    {
        std::fprintf(stderr, "%s\n", !shader ? shader.error().c_str() : device.error().c_str());
        return 1;
    }
    auto result = vultra::testing::transparencyDepthProbe(**device, *shader);
    if (!result)
    {
        std::fprintf(stderr, "%s\n", result.error().c_str());
        return 1;
    }
    std::puts("Transparency rejects occluded fragments, accepts equal and nearer depth, and preserves stored depth");
    return 0;
}
