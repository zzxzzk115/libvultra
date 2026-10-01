#pragma once

struct GLFWwindow;

namespace vultra
{
    class Input;

    namespace platform
    {
        // Owned windows only. ImGui installs its chaining callbacks afterwards.
        void installGlfwInput(GLFWwindow* window, Input& input);
    } // namespace platform
} // namespace vultra
