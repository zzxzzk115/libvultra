#include "../common/mode.hpp"

#include <array>

#ifdef VULTRA_EXAMPLE_HELMET
int runHelmet(int argc, char** argv);
#endif
#ifdef VULTRA_EXAMPLE_DEBUG
int runDebugDraw(int argc, char** argv);
#endif
#ifdef VULTRA_EXAMPLE_SPONZA
int runSponza(int argc, char** argv);
#endif
#ifdef VULTRA_EXAMPLE_SPONZA_MESH_SHADING
int runMeshlets(int argc, char** argv);
#endif

int main(int argc, char** argv)
{
    constexpr std::array modes {
#ifdef VULTRA_EXAMPLE_HELMET
        sample::Mode {"helmet", "glTF viewer with OpenPBR and shadows", runHelmet},
#endif
#ifdef VULTRA_EXAMPLE_DEBUG
        sample::Mode {"debug", "Helmet bounds, grid, axes and wireframes", runDebugDraw},
#endif
#ifdef VULTRA_EXAMPLE_SPONZA
        sample::Mode {"sponza", "Sponza scene and first-person camera", runSponza},
#endif
#ifdef VULTRA_EXAMPLE_SPONZA_MESH_SHADING
        sample::Mode {"sponza-mesh-shading", "Sponza mesh shader and indexed comparison", runMeshlets},
#endif
    };
    return sample::runMode(argc, argv, modes);
}
