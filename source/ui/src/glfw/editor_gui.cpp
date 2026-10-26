#include <vultra/platform/os/window.hpp>
#include <vultra/ui/editor_gui.hpp>

#include <backends/imgui_impl_glfw.h>

namespace vultra
{
    bool EditorGui::initializePlatform()
    {
        return ImGui_ImplGlfw_InitForVulkan(static_cast<GLFWwindow*>(m_Window->handle()), true);
    }

    void EditorGui::shutdownPlatform()
    {
        ImGui_ImplGlfw_Shutdown();
    }

    void EditorGui::beginPlatformFrame()
    {
        ImGui_ImplGlfw_NewFrame();
    }

    float EditorGui::platformDpiScale() const
    {
        const auto scale = ImGui_ImplGlfw_GetContentScaleForWindow(static_cast<GLFWwindow*>(m_Window->handle()));
        return scale > 0 ? scale : 1.0f;
    }

    void* EditorGui::viewportHandle(ImGuiViewport* viewport)
    {
        return viewport->PlatformHandle;
    }
} // namespace vultra
