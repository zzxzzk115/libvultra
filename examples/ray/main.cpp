#include "../common/mode.hpp"

#include <array>

#ifdef VULTRA_EXAMPLE_TRIANGLE
int runRayTriangle(int argc, char** argv);
#endif
#ifdef VULTRA_EXAMPLE_CORNELL
int runCornellBox(int argc, char** argv);
#endif
#ifdef VULTRA_EXAMPLE_QUERY
int runRayQuery(int argc, char** argv);
#endif

int main(int argc, char** argv)
{
    constexpr std::array modes {
#ifdef VULTRA_EXAMPLE_TRIANGLE
        sample::Mode {"triangle", "Raygen, miss and closest-hit triangle", runRayTriangle},
#endif
#ifdef VULTRA_EXAMPLE_CORNELL
        sample::Mode {"cornell", "Primary and shadow rays in Cornell Box", runCornellBox},
#endif
#ifdef VULTRA_EXAMPLE_QUERY
        sample::Mode {"query", "Rasterization with fragment-stage ray query shadows", runRayQuery},
#endif
    };
    return sample::runMode(argc, argv, modes);
}
