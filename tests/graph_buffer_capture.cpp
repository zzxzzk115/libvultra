#include "support/graph_buffer_capture_probe.hpp"

#include <cstdio>

int main(int, char**)
{
    auto device = vultra::Device::create(true, nullptr, 0);
    if (!device)
    {
        std::fprintf(stderr, "%s\n", device.error().c_str());
        return 1;
    }
    if (auto checked = vultra::test::graph_buffer_capture_probe::run(**device); !checked)
    {
        std::fprintf(stderr, "%s\n", checked.error().c_str());
        return 1;
    }
    return 0;
}
