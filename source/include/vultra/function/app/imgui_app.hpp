#pragma once

#include <vultra/function/app/desktop_app.hpp>
#include <vultra/function/renderer/gui.hpp>

namespace vultra
{
    // Owns the GUI frame. Scene rendering and the position of the GUI pass remain explicit.
    class ImGuiApp : public DesktopApp
    {
    public:
        explicit ImGuiApp(const DesktopAppConfig& config = {}, const GuiConfig& guiConfig = {});

    protected:
        // Call the base first to finalize UI capture; raw input comes from getWindow().input().
        void         onPreRender() override;
        virtual void onImGui();

        // Overlay an already rendered target, outside any active rendering pass.
        void drawGui(VriCommandBuffer* cmd, Texture& target);

        // For an explicit RenderGraph pass: copy() outside, draw() inside its rendering pass.
        Gui& getGui()
        {
            return m_Gui;
        }

    private:
        void onPostPresent() final;
        void onRenderSkipped() final;
        Gui  m_Gui;
    };
} // namespace vultra
