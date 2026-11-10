#include "support/graph_inspection_expected_probe.hpp"

#include <cstdio>

int main()
{
    auto device = vultra::Device::create(true, nullptr, 0);
    if (!device)
    {
        std::fprintf(stderr, "%s\n", device.error().c_str());
        return 1;
    }
    if (auto checked = vultra::test::graph_inspection_probe::run(**device); !checked)
    {
        std::fprintf(stderr, "%s\n", checked.error().c_str());
        return 1;
    }
    return 0;
}
