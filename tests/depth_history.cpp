#include "support/depth_history_msaa_probe.hpp"
#include "support/depth_history_probe.hpp"

#include <cstdio>
#include <fstream>

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::fputs("Usage: test-depth-history depth-history.vshaderc\n", stderr);
        return 2;
    }
    std::ifstream input(argv[1], std::ios::binary | std::ios::ate);
    if (!input || input.tellg() <= 0 || input.tellg() > 32 * 1024 * 1024)
    {
        std::fputs("Cannot open the MSAA history sampling program\n", stderr);
        return 1;
    }
    std::vector<std::byte> bytes(static_cast<size_t>(input.tellg()));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size())))
    {
        return 1;
    }
    auto shader = vultra::ShaderProgram::loadExpected(bytes, argv[1]);
    if (!shader)
    {
        std::fprintf(stderr, "%s\n", shader.error().c_str());
        return 1;
    }
    auto device = vultra::Device::create(true, nullptr, 0);
    if (!device)
    {
        std::fprintf(stderr, "%s\n", device.error().c_str());
        return 1;
    }
    if (auto result = vultra::test::depth_history_probe::probe(**device); !result)
    {
        std::fprintf(stderr, "%s\n", result.error().c_str());
        return 1;
    }
    if (auto result = vultra::test::depth_history_msaa_probe::probe(**device, *shader); !result)
    {
        std::fprintf(stderr, "%s\n", result.error().c_str());
        return 1;
    }
    return 0;
}
