#include "../common/mode.hpp"

#include <array>

#ifdef VULTRA_EXAMPLE_WINDOW
int runWindow(const vultra::AppLaunch& launch, int argc, char** argv);
#endif
#ifdef VULTRA_EXAMPLE_VRI
int runVriTriangle(const vultra::AppLaunch& launch, int argc, char** argv);
#endif
#ifdef VULTRA_EXAMPLE_GRAPH
int runRenderGraph(const vultra::AppLaunch& launch, int argc, char** argv);
#endif
#ifdef VULTRA_EXAMPLE_MESH_SHADING
int runMeshShader(const vultra::AppLaunch& launch, int argc, char** argv);
#endif

int runExample(const vultra::AppLaunch& launch, int argc, char** argv)
{
    constexpr std::array modes {
#ifdef VULTRA_EXAMPLE_WINDOW
        sample::Mode {"window", "Window and swapchain clear", runWindow},
#endif
#ifdef VULTRA_EXAMPLE_VRI
        sample::Mode {"vri", "Indexed triangle through VRI", runVriTriangle},
#endif
#ifdef VULTRA_EXAMPLE_GRAPH
        sample::Mode {"graph", "Triangle through RenderGraph", runRenderGraph},
#endif
#ifdef VULTRA_EXAMPLE_MESH_SHADING
        sample::Mode {"mesh-shading", "Task and mesh shader triangle", runMeshShader},
#endif
    };
    return sample::runMode(launch, argc, argv, modes);
}
