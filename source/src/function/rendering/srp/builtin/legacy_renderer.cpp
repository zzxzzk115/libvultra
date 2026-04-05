#include "vultra/function/rendering/srp/builtin/legacy_renderer.hpp"

#include "vultra/function/rendering/srp/builtin/features/final_composition_feature.hpp"
#include "vultra/function/rendering/srp/builtin/features/legacy_mesh_feature.hpp"

#include <imgui.h>

namespace vultra
{
    namespace
    {
        void drawFpsOverlay(const char* windowName)
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

            if (ImGui::Begin(windowName, nullptr, kFlags))
            {
                const float fps = ImGui::GetIO().Framerate;
                const float ms  = fps > 0.0f ? (1000.0f / fps) : 0.0f;

                ImGui::Text("FPS: %.1f", fps);
                ImGui::Text("Frame: %.2f ms", ms);
            }
            ImGui::End();
        }
    } // namespace

    LegacyRenderer::LegacyRenderer(std::string name, const LegacyRendererProfile profile) :
        m_Name(std::move(name)), m_Profile(profile)
    {
    }

    void LegacyRenderer::init()
    {
        if (m_FeaturesInitialized)
            return;

        emplaceFeature<LegacyMeshFeature>(m_Profile);
        if (m_Profile == LegacyRendererProfile::eVulkanCompat)
            emplaceFeature<FinalCompositionFeature>();

        m_FeaturesInitialized = true;
    }

    void LegacyRenderer::render(ImmediateRenderContext& ctx)
    {
        ctx.clear();
    }

    void LegacyRenderer::onImGui()
    {
        const char* overlayName = m_Profile == LegacyRendererProfile::eWebGPUCompat ?
                                      "##WebGPUCompatRendererFpsOverlay" :
                                      "##AndroidCompatRendererFpsOverlay";
        drawFpsOverlay(overlayName);
    }
} // namespace vultra

