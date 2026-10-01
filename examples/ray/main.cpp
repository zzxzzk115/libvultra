#include "../common/mode.hpp"

#include <array>

int runRayTriangle(int argc, char** argv);
int runCornellBox(int argc, char** argv);
int runRayQuery(int argc, char** argv);

int main(int argc, char** argv)
{
    constexpr std::array modes {
        sample::Mode {"triangle", "Raygen, miss and closest-hit triangle", runRayTriangle},
        sample::Mode {"cornell", "Primary and shadow rays in Cornell Box", runCornellBox},
        sample::Mode {"query", "Rasterization with fragment-stage ray query shadows", runRayQuery},
    };
    return sample::runMode(argc, argv, modes);
}
