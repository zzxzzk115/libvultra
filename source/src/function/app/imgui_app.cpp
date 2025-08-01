#include "vultra/function/app/imgui_app.hpp"
#include "vultra/function/renderer/imgui_renderer.hpp"

namespace vultra
{
    ImGuiApp::ImGuiApp(std::span<char*>         args,
                       const AppConfig&         appConfig,
                       const ImGuiConfig&       imguiConfig,
                       std::optional<glm::vec4> clearColor) : BaseApp {args, appConfig}, m_ClearColor(clearColor)
    {
        imgui::ImGuiRenderer::initImGui(
            *m_RenderDevice, m_Swapchain, m_Window, imguiConfig.enableMultiviewport, imguiConfig.enableDocking);
    }

    ImGuiApp::~ImGuiApp() { imgui::ImGuiRenderer::shutdown(); }

    void ImGuiApp::onGeneralWindowEvent(const os::GeneralWindowEvent& event)
    {
        imgui::ImGuiRenderer::processEvent(event);
        BaseApp::onGeneralWindowEvent(event);
    }

    void ImGuiApp::drawGui(rhi::CommandBuffer& cb, const rhi::RenderTargetView rtv)
    {
        const auto& [frameIndex, target] = rtv;
        rhi::prepareForAttachment(cb, target, false);

        imgui::ImGuiRenderer::render(cb,
                                     {.area             = {.extent = target.getExtent()},
                                      .colorAttachments = {
                                          {.target = &target, .clearValue = std::move(m_ClearColor)},
                                      }});
    }

    void ImGuiApp::onPostUpdate(const fsec dt)
    {
        auto& io     = ImGui::GetIO();
        io.DeltaTime = dt.count();
    }

    void ImGuiApp::onPreRender()
    {
        imgui::ImGuiRenderer::begin();
        onImGui();
        imgui::ImGuiRenderer::end();
    }

    void ImGuiApp::onRender(rhi::CommandBuffer& cb, const rhi::RenderTargetView rtv, const fsec)
    {
        ZoneScopedN("ImGuiApp::onRender");
        drawGui(cb, rtv);
    }

    void ImGuiApp::onPostRender() { imgui::ImGuiRenderer::postRender(); }
} // namespace vultra