#include <vultra/function/app/imgui_app.hpp>

namespace vultra
{
    ImGuiApp::ImGuiApp(const DesktopAppConfig& config, const GuiConfig& guiConfig) :
        DesktopApp(config),
        m_Gui(getDevice(), getWindow(), getSwapchain().format(), guiConfig)
    {
    }

    void ImGuiApp::onPreRender()
    {
        m_Gui.begin();
        onImGui();
        m_Gui.upload(getSwapchain().size());
    }

    void ImGuiApp::onImGui()
    {
    }

    void ImGuiApp::onRenderSkipped()
    {
        if (ImGui::GetPlatformIO().Viewports.Size > 1)
        {
            m_Gui.begin();
            onImGui();
            m_Gui.upload({}); // No main framebuffer; platform windows keep their own draw data.
            m_Gui.renderPlatformWindows();
        }
    }

    void ImGuiApp::onPostPresent()
    {
        m_Gui.renderPlatformWindows();
    }

    void ImGuiApp::drawGui(VriCommandBuffer* cmd, Texture& target)
    {
        m_Gui.copy(cmd);
        target.transition(cmd,
                          {VriAccess_ColorAttachmentRead | VriAccess_ColorAttachmentWrite,
                           VriLayout_ColorAttachment,
                           VriPipelineStage_ColorAttachmentOutput});
        beginColorPass(getDevice(), cmd, target.view(), {target.desc.width, target.desc.height});
        m_Gui.draw(cmd);
        getDevice().core.CmdEndRendering(cmd);
    }
} // namespace vultra
