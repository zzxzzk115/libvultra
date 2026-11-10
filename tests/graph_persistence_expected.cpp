#include "support/graph_persistence_expected_probe.hpp"

#include <cstdio>

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::fprintf(stderr, "Expected a fresh graph persistence directory\n");
        return 1;
    }
    if (auto checked = vultra::test::graphPersistenceExpectedProbe(argv[1]); !checked)
    {
        std::fprintf(stderr, "%s\n", checked.error().c_str());
        return 1;
    }
    return 0;
}
