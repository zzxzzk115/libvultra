#include <vultra/core/app/demo_app_host.hpp>
#include <vultra/core/base/common_context.hpp>
#include <vultra/function/rendering/srp/builtin/features/final_composition_feature.hpp>
#include <vultra/function/rendering/srp/builtin/features/gaussian_splat_feature.hpp>
#include <vultra/function/rendering/srp/builtin/features/meshlet_feature.hpp>
#include <vultra/function/rendering/srp/builtin/features/test_feature.hpp>
#include <vultra/function/rendering/srp/renderer.hpp>
#include <vultra/function/services/frame_debugger_service.hpp>

#include <imgui.h>

using namespace vultra;

class ImGuiExampleRenderer final : public FeatureRenderer
{
public:
    std::string_view name() const override { return "imgui_example"; }

    void init() override
    {
        emplaceFeature<MeshletFeature>();
        emplaceFeature<GaussianSplatFeature>();
        emplaceFeature<TestFeature>();
        emplaceFeature<FinalCompositionFeature>();
    }

    void onImGui() override
    {
        ImGui::ShowDemoWindow();
        ImGui::Begin("Example Window", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("Hello, world!");
        ImGui::Text("This example runs with DemoAppHost + FeatureRenderer.");

#ifdef VULTRA_ENABLE_RENDERDOC
        if (ImGui::Button("Capture One Frame"))
        {
            if (auto* frameDebuggerService = getServices()->tryGet<IFrameDebuggerService>(); frameDebuggerService)
                frameDebuggerService->captureSingleFrame();
        }
#endif
        ImGui::End();
    }
};

class ImGuiExampleApp final : public DemoAppHost
{
protected:
    std::string_view demoWindowTitle() const override { return "ImGui Example"; }

    Ref<Renderer> makeRenderer() const override
    {
        return createRef<ImGuiExampleRenderer>();
    }
};

int main()
{
    ImGuiExampleApp app {};
    return app.run();
}