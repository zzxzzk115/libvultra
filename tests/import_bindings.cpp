#include "support/import_bindings_probe.hpp"

#include <cstdio>

int main()
{
    auto device = vultra::Device::create(true, nullptr, 0);
    if (!device)
    {
        std::fprintf(stderr, "%s\n", device.error().c_str());
        return 1;
    }
    if (auto checked = vultra::test::import_bindings_probe::boundaries(**device); !checked)
    {
        std::fprintf(stderr, "%s\n", checked.error().c_str());
        return 1;
    }
    for (unsigned mode = 0; mode < 3; ++mode)
    {
        if (auto checked = vultra::test::import_bindings_probe::workload(**device, mode); !checked)
        {
            std::fprintf(stderr, "%s\n", checked.error().c_str());
            return 1;
        }
    }
    return 0;
}
