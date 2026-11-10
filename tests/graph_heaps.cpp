#include "support/graph_heap_formats_probe.hpp"
#include "support/graph_heap_images_probe.hpp"
#include "support/graph_heap_packing_probe.hpp"
#include "support/graph_heaps_probe.hpp"
#include "support/graph_host_memory_probe.hpp"

#include <cstdio>

int main(int, char**)
{
    auto device = vultra::Device::create(true, nullptr, 0);
    if (!device)
    {
        std::fprintf(stderr, "%s\n", device.error().c_str());
        return 1;
    }
    if (auto result = vultra::test::graphHeapsProbe(**device); !result)
    {
        std::fprintf(stderr, "%s\n", result.error().c_str());
        return 1;
    }
    if (auto result = vultra::test::graph_heap_images_probe::probe(**device); !result)
    {
        std::fprintf(stderr, "%s\n", result.error().c_str());
        return 1;
    }
    if (auto result = vultra::test::graph_heap_packing_probe::packedRegions(**device); !result)
    {
        std::fprintf(stderr, "%s\n", result.error().c_str());
        return 1;
    }
    if (auto result = vultra::test::graph_heap_packing_probe::metadataRejections(**device); !result)
    {
        std::fprintf(stderr, "%s\n", result.error().c_str());
        return 1;
    }
    if (auto result = vultra::test::graph_host_memory_probe::probe(**device); !result)
    {
        std::fprintf(stderr, "%s\n", result.error().c_str());
        return 1;
    }
    if (auto result = vultra::test::graph_heap_formats_probe::probe(**device); !result)
    {
        std::fprintf(stderr, "%s\n", result.error().c_str());
        return 1;
    }
    return 0;
}
