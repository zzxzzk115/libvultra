#pragma once
#include <vultra/core/rhi/resources.hpp>
#include <vultra/function/renderer/gui_theme.hpp>

#include <imgui.h>

#include <filesystem>
#include <string>
#include <vector>

namespace vultra
{
    struct GuiConfig
    {
        bool                  docking       = true;
        bool                  multiViewport = true;
        bool                  persistLayout = true; // false disables both automatic loading and saving.
        std::string           appName;              // Empty uses the executable stem, independent of the window title.
        std::filesystem::path iniFile;              // Empty uses .vultra/<appName>/imgui.ini in the run directory.
        GuiTheme              theme = GuiTheme::eUnreal;
    };

    // GLFW supplies native windows; VRI renders the main and detached viewports.
    class Gui
    {
    public:
        Gui(Device& device, Window& window, VriFormat targetFormat, const GuiConfig& config = {});
        ~Gui();
        Gui(const Gui&)                    = delete;
        Gui&         operator=(const Gui&) = delete;
        void         begin();
        void         setTheme(GuiTheme theme);
        InputCapture inputCapture() const;

        GuiTheme theme() const
        {
            return m_Theme;
        }

        ImGuiID dockspaceId() const
        {
            return m_Dockspace;
        }

        void        upload(Extent framebuffer);  // ends ImGui frame; call before recording GUI commands
        void        copy(VriCommandBuffer* cmd); // outside a rendering pass
        void        draw(VriCommandBuffer* cmd); // inside a rendering pass
        ImTextureID textureId(Texture& texture);
        // Previous GPU frame must be complete. Call before destroying/resizing a displayed texture.
        void forgetTexture(Texture& texture);
        // Call once after the main GPU frame/present, while sampled textures are still alive.
        void renderPlatformWindows();

    private:
        struct Viewport;

        struct DrawData
        {
            void                             update(const ImDrawData* draw, Extent framebuffer);
            VriImguiDrawData                 data {};
            std::vector<VriImguiVertex>      vertices;
            std::vector<ImDrawIdx>           indices;
            std::vector<VriImguiDrawCommand> commands;
        };

        void              installViewportCallbacks();
        Device&           m_Device;
        std::string       m_IniFile;
        VriImguiInterface m_Api {};
        VriImgui*         m_Renderer = nullptr;
        ImGuiContext*     m_Context  = nullptr;
        DrawData          m_DrawData;
        ImGuiID           m_Dockspace = 0;
        GuiTheme          m_Theme     = GuiTheme::eUnreal;
    };
} // namespace vultra
