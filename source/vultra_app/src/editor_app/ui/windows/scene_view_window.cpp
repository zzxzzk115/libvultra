#include "editor_app/ui/windows/scene_view_window.hpp"

#include <imgui.h>

namespace vultra_app
{
    SceneViewWindow::SceneViewWindow() : EditorWindow("Scene View") {}

    void SceneViewWindow::draw(EditorContext&)
    {
        ImGui::Begin(m_Name.c_str(), &m_Open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        ImGui::InvisibleButton("##SceneViewCanvas", avail);
        const auto min = ImGui::GetItemRectMin();
        const auto max = ImGui::GetItemRectMax();
        auto*      dl  = ImGui::GetWindowDrawList();
        dl->AddRectFilled(min, max, IM_COL32(22, 24, 28, 255));
        dl->AddRect(min, max, IM_COL32(70, 80, 96, 255));
        dl->AddText(ImVec2(min.x + 16.0f, min.y + 16.0f), IM_COL32(190, 200, 215, 255), "Scene View");
        dl->AddText(ImVec2(min.x + 16.0f, min.y + 38.0f), IM_COL32(125, 135, 150, 255), "Renderer viewport hookup will live here.");
        ImGui::End();
    }
} // namespace vultra_app
