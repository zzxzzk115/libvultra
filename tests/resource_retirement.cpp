#include "support/resource_retirement_shader_probe.hpp"

#include <cstdio>

int main(int argc, char** argv)
{
    if (argc != 4)
    {
        std::fprintf(stderr, "Expected two cooked shader programs and a writable fixture directory\n");
        return 1;
    }
    auto device = vultra::Device::create(true, nullptr, 0);
    if (!device)
    {
        std::fprintf(stderr, "%s\n", device.error().c_str());
        return 1;
    }
    if (auto result = vultra::test::resource_retirement_probe::probe(**device); !result)
    {
        std::fprintf(stderr, "%s\n", result.error().c_str());
        return 1;
    }
    if (auto result = vultra::test::resource_retirement_probe::shaderReplacement(**device, argv[1], argv[2], argv[3]);
        !result)
    {
        std::fprintf(stderr, "%s\n", result.error().c_str());
        return 1;
    }
    auto program = vultra::ShaderProgram::loadExpected(argv[1]);
    if (!program)
    {
        std::fprintf(stderr, "%s\n", program.error().c_str());
        return 1;
    }
    if (auto result = vultra::test::resource_retirement_probe::sceneRetirement(**device, *program); !result)
    {
        std::fprintf(stderr, "%s\n", result.error().c_str());
        return 1;
    }
    return 0;
}
