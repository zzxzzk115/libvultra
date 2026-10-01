#include "../common/mode.hpp"

#include <array>

int runWindow(int argc, char** argv);
int runVriTriangle(int argc, char** argv);
int runRenderGraph(int argc, char** argv);
int runMeshShader(int argc, char** argv);

int main(int argc, char** argv)
{
    constexpr std::array modes {
        sample::Mode {"window", "Window and swapchain clear", runWindow},
        sample::Mode {"vri", "Indexed triangle through VRI", runVriTriangle},
        sample::Mode {"graph", "Triangle through RenderGraph", runRenderGraph},
        sample::Mode {"mesh-shading", "Task and mesh shader triangle", runMeshShader},
    };
    return sample::runMode(argc, argv, modes);
}
