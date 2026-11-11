#include "../common/colored_mesh.hpp"
#include "../common/sample.hpp"

#include <vultra/app/imgui_app.hpp>

#include <imgui.h>

#include <optional>

class UiShowcaseApp final : public vultra::ImGuiApp
{
public:
    static std::expected<std::unique_ptr<UiShowcaseApp>, vultra::Error>
    create(Services services, const vultra::AppConfig& config, const sample::Options& options)
    {
        auto triangle = sample::ColoredMesh::create(services.device(),
                                                    services.swapchain().format(),
                                                    sample::kTriangleVertices,
                                                    sample::kTriangleIndices);
        if (!triangle)
        {
            return std::unexpected(triangle.error());
        }
        return std::unique_ptr<UiShowcaseApp>(
            new UiShowcaseApp(std::move(services), config, options, std::move(*triangle)));
    }

    ~UiShowcaseApp() override
    {
        if (m_Target)
        {
            if (auto forgotten = getEditorGui().forgetTexture(*m_Target); !forgotten)
            {
                vultra::Logger::app().error("{}", forgotten.error().message);
            }
        }
    }

private:
    UiShowcaseApp(Services                             services,
                  const vultra::AppConfig&             config,
                  const sample::Options&               options,
                  std::unique_ptr<sample::ColoredMesh> triangle) :
        vultra::ImGuiApp(std::move(services), config),
        m_Options(options),
        m_Triangle(std::move(triangle))
    {
    }

    std::expected<void, vultra::Error> onResize(vultra::Extent size) override
    {
        if (m_Target)
        {
            if (auto forgotten = getEditorGui().forgetTexture(*m_Target); !forgotten)
            {
                return forgotten;
            }
        }
        auto target = vultra::Texture::create(getDevice(), vultra::colorTexture(size, getSwapchain().format()));
        if (!target)
        {
            return std::unexpected(target.error());
        }
        m_Target = std::move(*target);
        return {};
    }

    std::expected<void, vultra::Error> onUpdate([[maybe_unused]] float deltaSeconds) override
    {
        m_SaveTarget = false;
        m_Triangle->pipeline->poll();
        return {};
    }

    std::expected<void, vultra::Error> onImGui() override
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
            if (!m_Triangle->pipeline->diagnostics().empty())
            {
                ui.textWrapped("%s", m_Triangle->pipeline->diagnostics().c_str());
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
                    const auto texture = getEditorGui().textureId(*m_Target);
                    if (!texture)
                    {
                        return std::unexpected(texture.error());
                    }
                    ui.image(*texture, {m_Target->desc.width * scale, m_Target->desc.height * scale});
                }
            }
        }
        return {};
    }

    std::expected<void, vultra::Error> onRender(VriCommandBuffer* cmd, vultra::Texture& target) override
    {
        m_Triangle->parameters.tint[1] = m_WarmTint ? 0.55f : 1.0f;
        m_Triangle->parameters.tint[2] = m_WarmTint ? 0.25f : 1.0f;
        m_Target->transition(
            cmd,
            {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
        if (auto drawn = m_Triangle->draw(cmd, *m_Target, m_Clear); !drawn)
        {
            return drawn;
        }
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
        return drawGui(cmd, target);
    }

    std::expected<void, vultra::Error> onPostRender(vultra::Texture& target) override
    {
        if (auto captured = sample::captureFrame(m_Options, frameCount(), getDevice(), target); !captured)
        {
            return captured;
        }
        if (!m_SaveTarget)
        {
            return {};
        }
        const auto image = vultra::readback(getDevice(), *m_Target);
        if (!image)
        {
            return std::unexpected(image.error());
        }
        return vultra::savePng(*image, "captures/imgui_render_target.png");
    }

    sample::Options                      m_Options;
    std::unique_ptr<sample::ColoredMesh> m_Triangle;
    std::optional<vultra::Texture>       m_Target;
    bool                                 m_ShowDemo   = false;
    bool                                 m_ShowTarget = true;
    bool                                 m_WarmTint   = false;
    bool                                 m_SaveTarget = false;
    float                                m_Clear[4] {0, 0, 0, 1};
};

int main(int argc, char** argv)
{
    const auto options = sample::readOptions(argc, argv);
    if (!options)
    {
        return sample::fail(options.error());
    }
    if (!*options)
    {
        return 0;
    }
    const vultra::AppConfig config {.title = "Vultra | UI - ImGui and EditorGui", .size = {1440, 900}};
    auto                    services = vultra::ImGuiApp::createServices(config);
    if (!services)
    {
        return sample::fail(services.error());
    }
    const auto app = UiShowcaseApp::create(std::move(*services), config, **options);
    if (!app)
    {
        return sample::fail(app.error());
    }
    if (auto result = (*app)->run((*options)->frames); !result)
    {
        return sample::fail(result.error());
    }
    vultra::Logger::app().info("UI showcase: {} frames", (*app)->frameCount());
    return 0;
}
