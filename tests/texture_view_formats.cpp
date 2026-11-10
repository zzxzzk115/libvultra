#include "support/texture_view_formats_probe.hpp"

#include <cstdio>

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::fprintf(stderr, "Expected a cooked texture-view format program\n");
        return 1;
    }
    auto program = vultra::ShaderProgram::loadExpected(argv[1]);
    auto device  = vultra::Device::create(true, nullptr, 0);
    if (!program || !device)
    {
        std::fprintf(stderr, "%s\n", program ? device.error().c_str() : program.error().c_str());
        return 1;
    }
    if (auto checked = vultra::test::texture_view_formats_probe::boundaries(**device); !checked)
    {
        std::fprintf(stderr, "%s\n", checked.error().c_str());
        return 1;
    }
    if (auto checked = vultra::test::texture_view_formats_probe::gpu(**device, *program); !checked)
    {
        std::fprintf(stderr, "%s\n", checked.error().c_str());
        return 1;
    }
    return 0;
}
