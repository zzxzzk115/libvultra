#include "../common/mode.hpp"

#include <array>

int runXrTriangle(int argc, char** argv);
int runXrSponza(int argc, char** argv);

int main(int argc, char** argv)
{
    constexpr std::array modes {
        sample::Mode {"triangle", "Tracked stereo triangle and desktop mirror", runXrTriangle},
        sample::Mode {"sponza", "Per-eye Sponza renderer and camera rig", runXrSponza},
    };
    return sample::runMode(argc, argv, modes);
}
