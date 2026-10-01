#include "../common/mode.hpp"

#include <array>

int runHelmet(int argc, char** argv);
int runDebugDraw(int argc, char** argv);
int runSponza(int argc, char** argv);
int runMeshlets(int argc, char** argv);

int main(int argc, char** argv)
{
    constexpr std::array modes {
        sample::Mode {"helmet", "glTF viewer with OpenPBR and shadows", runHelmet},
        sample::Mode {"debug", "Helmet bounds, grid, axes and wireframes", runDebugDraw},
        sample::Mode {"sponza", "Sponza scene and first-person camera", runSponza},
        sample::Mode {"sponza-mesh-shading", "Sponza mesh shader and indexed comparison", runMeshlets},
    };
    return sample::runMode(argc, argv, modes);
}
