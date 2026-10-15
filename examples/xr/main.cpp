#include "../common/mode.hpp"

#include <array>

#ifdef VULTRA_EXAMPLE_TRIANGLE
int runXrTriangle(int argc, char** argv);
#endif
#ifdef VULTRA_EXAMPLE_SPONZA
int runXrSponza(int argc, char** argv);
#endif

int main(int argc, char** argv)
{
    constexpr std::array modes {
#ifdef VULTRA_EXAMPLE_TRIANGLE
        sample::Mode {"triangle", "Tracked stereo triangle and desktop mirror", runXrTriangle},
#endif
#ifdef VULTRA_EXAMPLE_SPONZA
        sample::Mode {"sponza", "Per-eye Sponza renderer and camera rig", runXrSponza},
#endif
    };
    return sample::runMode(argc, argv, modes);
}
