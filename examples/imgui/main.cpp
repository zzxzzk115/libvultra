#include <vultra/core/app/demo_app_host.hpp>
#include <vultra/function/services/frame_debugger_service.hpp>

#include <imgui.h>

using namespace vultra;

class ImGuiExampleRenderer final : public FeatureRenderer
{
public:
    std::string_view name() const override { return "imgui_example"; }

    void onImGui() override
    {
        ImGui::ShowDemoWindow();
        ImGui::Begin("Example Window", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("Hello, world!");
        ImGui::Text("This example runs with DemoAppHost + FeatureRenderer.");

        if (auto* frameDebuggerService = getServices()->tryGet<IFrameDebuggerService>();
            frameDebuggerService && frameDebuggerService->isAvailable())
        {
            if (ImGui::Button("Capture One Frame"))
                frameDebuggerService->captureSingleFrame();
        }
        ImGui::End();
    }
};

class ImGuiExampleApp final : public DemoAppHost
{
protected:
    std::string_view demoWindowTitle() const override { return "ImGui Example"; }

    bool demoEnableExperimentalWebGPUContent() const override { return true; }

    Ref<Renderer> makeRenderer() const override { return createRef<ImGuiExampleRenderer>(); }
};

int main(int argc, char** argv)
{
    ImGuiExampleApp app {};
    return app.run(argc, argv);
}
