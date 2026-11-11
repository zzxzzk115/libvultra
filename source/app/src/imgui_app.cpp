#include <vultra/app/imgui_app.hpp>

namespace vultra
{
    std::expected<ImGuiApp::Services, Error> ImGuiApp::createServices(const AppConfig&       config,
                                                                      const EditorGuiConfig& guiConfig)
    {
        auto context = RuntimeContext::create(config);
        if (!context)
        {
            return std::unexpected(context.error());
        }
        auto gui = EditorGui::create(context->device(), context->window(), context->swapchain().format(), guiConfig);
        if (!gui)
        {
            return std::unexpected(gui.error());
        }
        return Services(std::move(*context), std::move(*gui));
    }

    ImGuiApp::ImGuiApp(Services services, const AppConfig& config) :
        DesktopApp(std::move(services.m_Context), config),
        m_Gui(std::move(services.m_Gui))
    {
    }

    std::expected<void, Error> ImGuiApp::onPreRender()
    {
        if (auto begun = m_Gui->begin(); !begun)
        {
            return begun;
        }
        if (auto built = onImGui(); !built)
        {
            return built;
        }
        return m_Gui->upload(getSwapchain().size());
    }

    std::expected<void, Error> ImGuiApp::onImGui()
    {
        return {};
    }

    std::expected<void, Error> ImGuiApp::onRenderSkipped()
    {
        if (ImGui::GetPlatformIO().Viewports.Size <= 1)
        {
            return {};
        }
        if (auto begun = m_Gui->begin(); !begun)
        {
            return begun;
        }
        if (auto built = onImGui(); !built)
        {
            return built;
        }
        // No main framebuffer; platform windows keep their own draw data.
        if (auto uploaded = m_Gui->upload({}); !uploaded)
        {
            return uploaded;
        }
        return m_Gui->renderPlatformWindows();
    }

    std::expected<void, Error> ImGuiApp::onPostPresent()
    {
        return m_Gui->renderPlatformWindows();
    }

    std::expected<void, Error> ImGuiApp::drawGui(VriCommandBuffer* cmd, Texture& target)
    {
        m_Gui->copy(cmd);
        target.transition(cmd,
                          {VriAccess_ColorAttachmentRead | VriAccess_ColorAttachmentWrite,
                           VriLayout_ColorAttachment,
                           VriPipelineStage_ColorAttachmentOutput});
        const auto view = target.view();
        if (!view)
        {
            return std::unexpected(view.error());
        }
        beginColorPass(getDevice(), cmd, *view, {target.desc.width, target.desc.height});
        m_Gui->draw(cmd);
        getDevice().core.CmdEndRendering(cmd);
        return {};
    }
} // namespace vultra
