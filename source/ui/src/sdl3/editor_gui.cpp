#include <vultra/platform/os/window.hpp>
#include <vultra/ui/editor_gui.hpp>

#include <SDL3/SDL.h>
#include <backends/imgui_impl_sdl3.h>

namespace vultra
{
    bool EditorGui::initializePlatform()
    {
        if (!ImGui_ImplSDL3_InitForVulkan(static_cast<SDL_Window*>(m_Window->handle())))
        {
            return false;
        }
        m_Window->m_GuiEventHandler = [context = m_Context](const void* event)
        {
            auto* previous = ImGui::GetCurrentContext();
            ImGui::SetCurrentContext(context);
            ImGui_ImplSDL3_ProcessEvent(static_cast<const SDL_Event*>(event));
            ImGui::SetCurrentContext(previous);
        };
        return true;
    }

    void EditorGui::shutdownPlatform()
    {
        m_Window->m_GuiEventHandler = {};
        ImGui_ImplSDL3_Shutdown();
    }

    void EditorGui::beginPlatformFrame()
    {
        ImGui_ImplSDL3_NewFrame();
    }

    void* EditorGui::viewportHandle(ImGuiViewport* viewport)
    {
        // ImGui's SDL3 backend stores the window ID, not SDL_Window*, in PlatformHandle.
        return SDL_GetWindowFromID(SDL_WindowID(reinterpret_cast<uintptr_t>(viewport->PlatformHandle)));
    }
} // namespace vultra
