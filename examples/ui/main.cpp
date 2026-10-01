#include "../common/colored_mesh.hpp"
#include "../common/sample.hpp"

#include <vultra/api/native_plugin.hpp>
#include <vultra/main/app/imgui_app.hpp>
#include <vultra/ui/vgui.hpp>

#include <imgui.h>

#include <cstdlib>

class UiShowcaseApp final : public vultra::ImGuiApp
{
public:
    explicit UiShowcaseApp(const sample::Options& options) :
        vultra::ImGuiApp({"Vultra | UI - ImGui, EditorGui and VGui", {1440, 900}}),
        m_Options(options),
        m_Triangle(getDevice(), getSwapchain().format(), sample::kTriangleVertices, sample::kTriangleIndices)
    {
        m_VGui = std::make_unique<vultra::VGui>(getDevice(), getWindow(), getSwapchain().format());
        m_VGui->loadFont("resources/ui/LatoLatin-Regular.ttf");
        m_VGui->loadDocument("examples/ui/showcase.rml");
        m_VGui->bindChange("preview-toggle",
                           [this]
                           {
                               m_ShowTarget = m_VGui->isChecked("preview-toggle");
                           });
        m_VGui->bindChange("tint-toggle",
                           [this]
                           {
                               m_WarmTint = m_VGui->isChecked("tint-toggle");
                           });
        m_VGui->bindChange("sample-volume",
                           [this]
                           {
                               m_VGui->setText("sample-volume-value", m_VGui->value("sample-volume"));
                           });
        m_VGui->bindChange("sample-quality",
                           [this]
                           {
                               m_VGui->setText("sample-quality-value", m_VGui->value("sample-quality"));
                           });
        m_VGui->bindClick("save-image",
                          [this]
                          {
                              m_SaveTarget = true;
                          });
        m_VGui->bindClick("kenney-play",
                          [this]
                          {
                              m_VGui->setText("kenney-status", "Play pressed");
                          });
        m_VGui->bindClick("kenney-next",
                          [this]
                          {
                              m_VGui->setText("kenney-status", "Next selected");
                          });
        m_VGui->bindClick("kenney-apply",
                          [this]
                          {
                              m_VGui->setText("kenney-status", "Settings applied");
                          });
        m_VGui->bindClick("kenney-save",
                          [this]
                          {
                              m_VGui->setText("kenney-status", "Profile saved");
                          });
        m_VGui->bindChange("kenney-music",
                           [this]
                           {
                               m_VGui->setText("kenney-status",
                                               m_VGui->isChecked("kenney-music") ? "Music enabled" : "Music disabled");
                           });
        m_VGui->bindChange("kenney-effects",
                           [this]
                           {
                               m_VGui->setText("kenney-status",
                                               m_VGui->isChecked("kenney-effects") ? "Effects enabled" :
                                                                                     "Effects disabled");
                           });
        m_VGui->bindChange("kenney-normal",
                           [this]
                           {
                               if (m_VGui->isChecked("kenney-normal"))
                               {
                                   m_VGui->setText("kenney-status", "Normal difficulty");
                               }
                           });
        m_VGui->bindChange("kenney-hard",
                           [this]
                           {
                               if (m_VGui->isChecked("kenney-hard"))
                               {
                                   m_VGui->setText("kenney-status", "Hard difficulty");
                               }
                           });
        m_VGui->bindChange("kenney-volume",
                           [this]
                           {
                               m_VGui->setText("kenney-volume-value", m_VGui->value("kenney-volume"));
                           });
        m_VGui->bindChange("kenney-resolution",
                           [this]
                           {
                               m_VGui->setText("kenney-status", "Resolution: " + m_VGui->value("kenney-resolution"));
                           });
        m_VGui->bindChange("kenney-player",
                           [this]
                           {
                               m_VGui->setText("kenney-status", "Player: " + m_VGui->value("kenney-player"));
                           });
        if (const char* pluginPath = std::getenv("VULTRA_NATIVE_PLUGIN"); pluginPath && *pluginPath)
        {
            m_Plugin = std::make_unique<vultra::NativePlugin>(pluginPath);
        }
    }

    ~UiShowcaseApp() override
    {
        if (m_Target)
        {
            getEditorGui().forgetTexture(*m_Target);
        }
    }

private:
    void onResize(vultra::Extent size) override
    {
        if (m_Target)
        {
            getEditorGui().forgetTexture(*m_Target);
        }
        m_Target = std::make_unique<vultra::Texture>(getDevice(), vultra::colorTexture(size, getSwapchain().format()));
    }

    void onUpdate(float deltaSeconds) override
    {
        m_Triangle.pipeline->poll();
        if (m_Plugin)
        {
            m_Plugin->update(deltaSeconds);
        }
        m_SaveTarget = false;
    }

    void onImGui() override
    {
        const auto viewport = ImGui::GetMainViewport();
        const auto left     = viewport->Pos.x + (viewport->Size.x - 1368.0f) * 0.5f;
        const auto top      = viewport->Pos.y + 22.0f;
        ImGui::SetNextWindowPos({left, top}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({330, 245}, ImGuiCond_Always);
        if (ImGui::Begin("01 / Raw ImGui"))
        {
            ImGui::TextUnformatted("Direct Dear ImGui calls");
            ImGui::Separator();
            ImGui::Checkbox("Show render target", &m_ShowTarget);
            ImGui::Checkbox("Warm tint", &m_WarmTint);
            ImGui::ColorEdit3("Clear", m_Clear);
            ImGui::Checkbox("Show ImGui demo", &m_ShowDemo);
            if (ImGui::Button("Save render target PNG"))
            {
                m_SaveTarget = true;
            }
        }
        ImGui::End();

        auto ui = getEditorGui().frame();
        ui.setNextWindowPos({left + 346.0f, top}, ImGuiCond_Always);
        ui.setNextWindowSize({330, 245}, ImGuiCond_Always);
        if (vultra::EditorGuiWindow window(ui, "02 / EditorGui"); window)
        {
            ui.textUnformatted("Native C++ wrapper and property rows");
            ui.separator();
            if (vultra::EditorGuiInspector inspector(getEditorGui(), "UI state"); inspector)
            {
                inspector.boolField({"preview", "Show render target"}, &m_ShowTarget);
                inspector.boolField({"tint", "Warm tint"}, &m_WarmTint);
            }
            if (ui.button("Save render target PNG"))
            {
                m_SaveTarget = true;
            }
            if (ui.beginCombo("Theme", vultra::editorGuiThemeName(getEditorGui().theme())))
            {
                for (int i = 0; i < vultra::kGuiThemeCount; ++i)
                {
                    const auto theme = static_cast<vultra::EditorGuiTheme>(i);
                    if (ui.selectable(vultra::editorGuiThemeName(theme), theme == getEditorGui().theme()))
                    {
                        getEditorGui().setTheme(theme);
                    }
                }
                ui.endCombo();
            }
            if (!m_Triangle.pipeline->diagnostics().empty())
            {
                ui.textWrapped("%s", m_Triangle.pipeline->diagnostics().c_str());
            }
        }
        if (m_Plugin)
        {
            ui.setNextWindowPos({left + 1038.0f, top + 340.0f}, ImGuiCond_Always);
            ui.setNextWindowSize({330, 110}, ImGuiCond_Always);
            if (vultra::EditorGuiWindow window(ui, "Native Plugin"); window)
            {
                m_Plugin->gui(getEditorGui());
            }
        }
        if (m_ShowDemo)
        {
            ui.showDemoWindow(&m_ShowDemo);
        }
        if (m_ShowTarget)
        {
            ui.setNextWindowPos({left, top + 265.0f}, ImGuiCond_Always);
            ui.setNextWindowSize({676, 420}, ImGuiCond_Always);
            if (vultra::EditorGuiWindow window(ui, "Render Target Viewer", &m_ShowTarget); window)
            {
                ui.text("%u x %u", m_Target->desc.width, m_Target->desc.height);
                const auto  available = ui.contentRegionAvail();
                const float scale = std::min(available.x / m_Target->desc.width, available.y / m_Target->desc.height);
                if (scale > 0)
                {
                    ui.image(getEditorGui().textureId(*m_Target),
                             {m_Target->desc.width * scale, m_Target->desc.height * scale});
                }
            }
        }
    }

    void onPreRender() override
    {
        ImGuiApp::onPreRender();
        m_VGui->setChecked("preview-toggle", m_ShowTarget);
        m_VGui->setChecked("tint-toggle", m_WarmTint);
        m_VGui->setText("preview-state", m_ShowTarget ? "Preview visible" : "Preview hidden");
        m_VGui->update(getWindow().input(), getSwapchain().size());
    }

    void onRender(VriCommandBuffer* cmd, vultra::Texture& target) override
    {
        m_Triangle.parameters.tint[1] = m_WarmTint ? 0.55f : 1.0f;
        m_Triangle.parameters.tint[2] = m_WarmTint ? 0.25f : 1.0f;
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
        m_VGui->draw(cmd, target);
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

    sample::Options                       m_Options;
    sample::ColoredMesh                   m_Triangle;
    std::unique_ptr<vultra::Texture>      m_Target;
    std::unique_ptr<vultra::VGui>         m_VGui;
    std::unique_ptr<vultra::NativePlugin> m_Plugin;
    bool                                  m_ShowDemo   = false;
    bool                                  m_ShowTarget = true;
    bool                                  m_WarmTint   = false;
    bool                                  m_SaveTarget = false;
    float                                 m_Clear[4] {0, 0, 0, 1};
};

int main(int argc, char** argv)
try
{
    const auto options = sample::readOptions(argc, argv);
    if (!options)
    {
        return 0;
    }
    UiShowcaseApp app(*options);
    app.run(options->frames);
    vultra::Logger::app().info("UI showcase: {} frames", app.frameCount());
    return 0;
}
catch (const std::exception& error)
{
    vultra::Logger::app().error("{}", error.what());
    return 1;
}
