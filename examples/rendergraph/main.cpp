#include "example_passes.hpp"
#include "fg_panel.hpp"

#include <vultra/core/base/common_context.hpp>
#include <vultra/function/app/imgui_app.hpp>
#include <vultra/function/renderer/imgui_renderer.hpp>

#include <imgui.h>
#include <imgui_node_editor/imgui_node_editor.h>

using namespace vultra;

class RenderGraphExampleApp final : public ImGuiApp
{
public:
    explicit RenderGraphExampleApp(const std::span<char*>& args) :
        ImGuiApp(args,
                 {.title = "RenderGraph Example", .vSyncConfig = rhi::VerticalSync::eEnabled},
                 {},
                 glm::vec4 {0.1f, 0.1f, 0.1f, 1.0f})
    {
        m_Editor.initialize();
    }

    ~RenderGraphExampleApp() override { m_Editor.shutdown(); }

    void onImGui() override
    {
        ImGui::Begin("Render Graph Example");
        ImGui::Text("Hello, world!");
#ifdef VULTRA_ENABLE_RENDERDOC
        ImGui::Button("Capture One Frame");
        if (ImGui::IsItemClicked())
        {
            m_WantCaptureFrame = true;
        }
#endif
        m_Editor.draw();

        ImGui::End();
    }

    void onRender(rhi::CommandBuffer& cb, const rhi::RenderTargetView rtv, const fsec dt) override
    {
        const auto& [frameIndex, target] = rtv;
        ImGuiApp::onRender(cb, rtv, dt);
    }

private:
    FrameGraphEditorPanel m_Editor;
};

CONFIG_MAIN(RenderGraphExampleApp)