#include <vultra/core/base/common_context.hpp>
#include <vultra/function/app/imgui_app.hpp>
#include <vultra/function/renderer/imgui_renderer.hpp>

#include <imgui.h>
#include <imgui_node_editor.h>

using namespace vultra;
namespace ed = ax::NodeEditor;

class RenderGraphExampleApp final : public ImGuiApp
{
public:
    explicit RenderGraphExampleApp(const std::span<char*>& args) :
        ImGuiApp(args,
                 {.title = "RenderGraph Example", .vSyncConfig = rhi::VerticalSync::eEnabled},
                 {.enableDocking = false},
                 glm::vec4 {0.1f, 0.1f, 0.1f, 1.0f})
    {
        ed::Config config;
        config.SettingsFile = nullptr;
        m_Context           = ed::CreateEditor(&config);
    }

    ~RenderGraphExampleApp() override
    {
        if (m_Context)
        {
            ed::DestroyEditor(m_Context);
            m_Context = nullptr;
        }
    }

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
        showRenderGraphEditor();

        ImGui::End();
    }

    void onRender(rhi::CommandBuffer& cb, const rhi::RenderTargetView rtv, const fsec dt) override
    {
        const auto& [frameIndex, target] = rtv;
        ImGuiApp::onRender(cb, rtv, dt);
    }

private:
    static void drawPinIcon(bool filled, const ImVec4& color, float radius = 5.0f)
    {
        ImDrawList* drawList  = ImGui::GetWindowDrawList();
        ImVec2      cursorPos = ImGui::GetCursorScreenPos();
        ImVec2      center    = ImVec2(cursorPos.x + radius, cursorPos.y + radius);

        if (filled)
            drawList->AddCircleFilled(center, radius, ImColor(color));
        else
            drawList->AddCircle(center, radius, ImColor(color), 12, 1.5f);

        ImGui::Dummy(ImVec2(radius * 2, radius * 2));
    }

    static void showRenderGraphEditor()
    {
        static ed::EditorContext* context = nullptr;
        if (!context)
            context = ed::CreateEditor();

        ed::SetCurrentEditor(context);
        ed::Begin("RenderGraph Editor");

        // --- Node 1: GBuffer ---
        ed::BeginNode(1);
        {
            ImGui::TextUnformatted("GBuffer Pass");

            // Input pin
            ed::BeginPin(101, ed::PinKind::Input);
            drawPinIcon(true, ImVec4(0.3f, 0.8f, 1.0f, 1.0f));
            ImGui::SameLine();
            ImGui::Text("InColor");
            ed::EndPin();

            // Output pin
            ed::BeginPin(102, ed::PinKind::Output);
            ImGui::Text("OutColor");
            ImGui::SameLine();
            drawPinIcon(true, ImVec4(0.9f, 0.7f, 0.2f, 1.0f));
            ed::EndPin();
        }
        ed::EndNode();

        // --- Node 2: Lighting ---
        ed::BeginNode(2);
        {
            ImGui::TextUnformatted("Lighting Pass");

            ed::BeginPin(201, ed::PinKind::Input);
            drawPinIcon(true, ImVec4(0.3f, 0.8f, 1.0f, 1.0f));
            ImGui::SameLine();
            ImGui::Text("InColor");
            ed::EndPin();

            ed::BeginPin(202, ed::PinKind::Output);
            ImGui::Text("OutLight");
            ImGui::SameLine();
            drawPinIcon(true, ImVec4(0.9f, 0.7f, 0.2f, 1.0f));
            ed::EndPin();
        }
        ed::EndNode();

        // Link nodes
        ed::Link(1001, 102, 201, ImColor(80, 150, 255), 3.0f);

        // User interactions
        if (ed::BeginCreate())
        {
            ed::PinId start, end;
            if (ed::QueryNewLink(&start, &end))
            {
                if (ed::AcceptNewItem())
                {
                    VULTRA_CLIENT_INFO("Link created from {} to {}", start.Get(), end.Get());
                }
            }
        }
        ed::EndCreate();

        ed::End();
    }

private:
    ed::EditorContext* m_Context = nullptr;
};

CONFIG_MAIN(RenderGraphExampleApp)