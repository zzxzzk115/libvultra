#include "../common/colored_mesh.hpp"
#include "../common/sample.hpp"

#include <vultra/function/app/imgui_app.hpp>

class ImGuiDemoApp final : public vultra::ImGuiApp
{
public:
    explicit ImGuiDemoApp(const sample::Options& options) :
        vultra::ImGuiApp({"Vultra | ImGui - Offscreen Triangle and Texture Viewer", {1280, 800}}),
        m_Options(options),
        m_Triangle(getDevice(), getSwapchain().format(), sample::kTriangleVertices, sample::kTriangleIndices)
    {
    }

    ~ImGuiDemoApp() override
    {
        if (m_Target)
        {
            getGui().forgetTexture(*m_Target);
        }
    }

private:
    void onResize(vultra::Extent size) override
    {
        if (m_Target)
        {
            getGui().forgetTexture(*m_Target);
        }
        m_Target = std::make_unique<vultra::Texture>(getDevice(), vultra::colorTexture(size, getSwapchain().format()));
    }

    void onUpdate(float) override
    {
        m_Triangle.pipeline->poll();
        m_SaveTarget = false;
    }

    void onImGui() override
    {
        const auto origin = ImGui::GetMainViewport()->Pos;
        ImGui::SetNextWindowPos({origin.x + 20, origin.y + 20}, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(360, 220), ImGuiCond_FirstUseEver);
        ImGui::Begin("ImGui Example");
        ImGui::TextUnformatted("Offscreen triangle / texture viewer");
        ImGui::TextUnformatted("Dock panels or drag them outside the window.");
        if (ImGui::BeginCombo("Theme", vultra::guiThemeName(getGui().theme())))
        {
            for (int i = 0; i < vultra::kGuiThemeCount; ++i)
            {
                const auto theme = static_cast<vultra::GuiTheme>(i);
                if (ImGui::Selectable(vultra::guiThemeName(theme), theme == getGui().theme()))
                {
                    getGui().setTheme(theme);
                }
            }
            ImGui::EndCombo();
        }
        ImGui::ColorEdit3("Tint", m_Triangle.parameters.tint);
        ImGui::ColorEdit3("Clear", m_Clear);
        ImGui::Checkbox("Show ImGui demo", &m_ShowDemo);
        ImGui::Checkbox("Show render target", &m_ShowTarget);
        if (ImGui::Button("Save render target PNG"))
        {
            m_SaveTarget = true;
        }
        if (!m_Triangle.pipeline->diagnostics().empty())
        {
            ImGui::TextWrapped("%s", m_Triangle.pipeline->diagnostics().c_str());
        }
        ImGui::End();
        if (m_ShowDemo)
        {
            ImGui::SetNextWindowPos({origin.x + 780, origin.y + 20}, ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize({460, 720}, ImGuiCond_FirstUseEver);
            ImGui::ShowDemoWindow(&m_ShowDemo);
        }
        if (m_ShowTarget)
        {
            ImGui::SetNextWindowPos({origin.x + 20, origin.y + 270}, ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize({600, 440}, ImGuiCond_FirstUseEver);
            ImGui::Begin("Render Target Viewer", &m_ShowTarget);
            ImGui::Text("%u x %u", m_Target->desc.width, m_Target->desc.height);
            const auto  available = ImGui::GetContentRegionAvail();
            const float scale     = std::min(available.x / m_Target->desc.width, available.y / m_Target->desc.height);
            if (scale > 0)
            {
                ImGui::Image(getGui().textureId(*m_Target),
                             {m_Target->desc.width * scale, m_Target->desc.height * scale});
            }
            ImGui::End();
        }
    }

    void onRender(VriCommandBuffer* cmd, vultra::Texture& target) override
    {
        m_Target->transition(
            cmd,
            {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
        m_Triangle.draw(cmd, *m_Target, m_Clear);
        m_Target->transition(cmd, {VriAccess_CopySourceRead, VriLayout_CopySource, VriPipelineStage_Transfer});
        target.transition(cmd, {VriAccess_CopyDestinationWrite, VriLayout_CopyDestination, VriPipelineStage_Transfer});
        VriTextureCopyDesc copy {};
        copy.src.aspect   = VriImageAspect_Color;
        copy.dst.aspect   = VriImageAspect_Color;
        copy.src.layerNum = 1;
        copy.dst.layerNum = 1;
        getDevice().core.CmdCopyTexture(cmd, target.handle, m_Target->handle, &copy);
        m_Target->transition(cmd,
                             {VriAccess_ShaderResourceRead, VriLayout_ShaderResource, VriPipelineStage_FragmentShader});
        drawGui(cmd, target);
    }

    void onPostRender(vultra::Texture& target) override
    {
        sample::captureFrame(m_Options, frameCount(), getDevice(), target);
        if (m_SaveTarget)
        {
            vultra::savePng(vultra::readback(getDevice(), *m_Target), "captures/imgui_render_target.png");
        }
    }

    sample::Options                  m_Options;
    sample::ColoredMesh              m_Triangle;
    std::unique_ptr<vultra::Texture> m_Target;
    bool                             m_ShowDemo   = true;
    bool                             m_ShowTarget = true;
    bool                             m_SaveTarget = false;
    float                            m_Clear[4] {0, 0, 0, 1};
};

int main(int argc, char** argv)
try
{
    const auto options = sample::readOptions(argc, argv);
    if (!options)
    {
        return 0;
    }
    ImGuiDemoApp app(*options);
    app.run(options->frames);
    vultra::Logger::app().info("ImGui: {} frames", app.frameCount());
    return 0;
}
catch (const std::exception& error)
{
    vultra::Logger::app().error("{}", error.what());
    return 1;
}
