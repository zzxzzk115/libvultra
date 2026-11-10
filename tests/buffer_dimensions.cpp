#include "support/buffer_dimensions_probe.hpp"

#include <cstdio>

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::fputs("Usage: test-buffer-dimensions direct.vshaderc array.vshaderc\n", stderr);
        return 2;
    }
    auto program = vultra::ShaderProgram::loadExpected(argv[1]);
    if (!program)
    {
        std::fprintf(stderr, "%s\n", program.error().c_str());
        return 1;
    }
    auto arrayProgram = vultra::ShaderProgram::loadExpected(argv[2]);
    if (!arrayProgram)
    {
        std::fprintf(stderr, "%s\n", arrayProgram.error().c_str());
        return 1;
    }
    auto device = vultra::Device::create(true, nullptr, VriFeature_Bindless);
    if (!device)
    {
        std::fprintf(stderr, "%s\n", device.error().c_str());
        return 1;
    }
    auto result = vultra::testing::bufferDimensionsProbe(**device, *program, *arrayProgram);
    if (!result)
    {
        std::fprintf(stderr, "%s\n", result.error().c_str());
        return 1;
    }
    return 0;
}
