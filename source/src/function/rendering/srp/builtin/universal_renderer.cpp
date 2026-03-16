#include "vultra/function/rendering/srp/builtin/universal_renderer.hpp"
#include "vultra/function/rendering/srp/builtin/features/final_composition_feature.hpp"
#include "vultra/function/rendering/srp/builtin/features/gaussian_splat_feature.hpp"
#include "vultra/function/rendering/srp/builtin/features/meshlet_feature.hpp"
#include "vultra/function/rendering/srp/builtin/features/test_feature.hpp"
#ifdef VULTRA_ENABLE_RENDERDOC
#include "vultra/function/services/frame_debugger_service.hpp"
#endif

#include <imgui.h>

namespace vultra
{
    namespace
    {
        void drawFpsOverlay()
        {
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            if (!viewport)
                return;

            constexpr float kPadding = 10.0f;

            const ImVec2 windowPos {viewport->WorkPos.x + viewport->WorkSize.x - kPadding,
                                    viewport->WorkPos.y + kPadding};
            const ImVec2 windowPivot {1.0f, 0.0f};

            ImGui::SetNextWindowPos(windowPos, ImGuiCond_Always, windowPivot);
            ImGui::SetNextWindowBgAlpha(0.35f);

            constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                                ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;

            if (ImGui::Begin("##UniversalRendererFpsOverlay", nullptr, kFlags))
            {
                const float fps = ImGui::GetIO().Framerate;
                const float ms  = fps > 0.0f ? (1000.0f / fps) : 0.0f;

                ImGui::Text("FPS: %.1f", fps);
                ImGui::Text("Frame: %.2f ms", ms);
            }
            ImGui::End();
        }
    } // namespace

    void UniversalRenderer::init()
    {
        // Add features in the desired order.
        emplaceFeature<MeshletFeature>();
        emplaceFeature<GaussianSplatFeature>();
        emplaceFeature<TestFeature>();
        emplaceFeature<FinalCompositionFeature>();
    }

    void UniversalRenderer::onImGui()
    {
        drawFpsOverlay();

        ImGui::Begin("Universal Renderer");

#ifdef VULTRA_ENABLE_RENDERDOC
        ImGui::Button("Capture One Frame");
        if (ImGui::IsItemClicked())
        {
            getServices()->require<IFrameDebuggerService>().captureSingleFrame();
        }
#endif
        ImGui::End();
    }
} // namespace vultra