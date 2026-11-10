#include "support/explicit_memory_probe.hpp"

#include <cstdio>

int main()
{
    auto device = vultra::Device::create(true, nullptr, 0);
    if (!device)
    {
        std::fprintf(stderr, "%s\n", device.error().c_str());
        return 1;
    }
    if (auto result = vultra::test::explicitMemoryProbe(**device); !result)
    {
        std::fprintf(stderr, "%s\n", result.error().c_str());
        return 1;
    }
    return 0;
}
