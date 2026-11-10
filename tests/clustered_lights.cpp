#include "support/clustered_lights_probe.hpp"

#include <cstdio>
#include <fstream>

int main(int argc, char** argv)
{
    if (argc != 2)
        return 2;
    std::ifstream file(argv[1], std::ios::binary | std::ios::ate);
    if (!file || file.tellg() <= 0)
        return 1;
    std::vector<std::byte> bytes(size_t(file.tellg()));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size())))
        return 1;
    auto shader = vultra::ShaderProgram::loadExpected(bytes, argv[1]);
    auto device = vultra::Device::create(true);
    if (!shader || !device)
    {
        std::fprintf(stderr, "%s\n", shader ? device.error().c_str() : shader.error().c_str());
        return 1;
    }
    auto result = vultra::testing::clusteredLightsProbe(**device, *shader);
    if (!result)
    {
        std::fprintf(stderr, "%s\n", result.error().c_str());
        return 1;
    }
    std::puts("Clustered light masks conservatively cover 64 lights and three camera projections");
    return 0;
}
