#include "vultra/function/rendering/srp/builtin/android_compat_renderer.hpp"

#include "vultra/function/rendering/srp/builtin/features/android_mesh_feature.hpp"
#include "vultra/function/rendering/srp/builtin/features/final_composition_feature.hpp"

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

            if (ImGui::Begin("##AndroidCompatRendererFpsOverlay", nullptr, kFlags))
            {
                const float fps = ImGui::GetIO().Framerate;
                const float ms  = fps > 0.0f ? (1000.0f / fps) : 0.0f;

                ImGui::Text("FPS: %.1f", fps);
                ImGui::Text("Frame: %.2f ms", ms);
            }
            ImGui::End();
        }
    } // namespace

    void AndroidCompatRenderer::init()
    {
        if (m_FeaturesInitialized)
            return;
        emplaceFeature<AndroidMeshFeature>();
        emplaceFeature<FinalCompositionFeature>();
        m_FeaturesInitialized = true;
    }

    void AndroidCompatRenderer::render(ImmediateRenderContext& ctx)
    {
        ctx.clear();
    }

    void AndroidCompatRenderer::onImGui()
    {
        drawFpsOverlay();
    }
} // namespace vultra
