#include "common/ui_widgets.hpp"

#include <IconsMaterialDesignIcons.h>
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <string>

namespace vultra_app::ui
{
    namespace
    {
        bool hasExtension(const std::filesystem::path& path, std::initializer_list<const char*> exts)
        {
            auto ext = path.extension().generic_string();
            std::transform(ext.begin(),
                           ext.end(),
                           ext.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            return std::any_of(exts.begin(), exts.end(), [&](const char* candidate) { return ext == candidate; });
        }

        void tooltip(const char* text)
        {
            if (text == nullptr || text[0] == '\0' || !ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                return;
            ImGui::SetTooltip("%s", text);
        }
    } // namespace

    const char* sourceAssetIcon(const std::filesystem::path& path, const bool isDirectory)
    {
        if (isDirectory)
            return ICON_MDI_FOLDER;
        if (hasExtension(path, {".png", ".jpg", ".jpeg", ".bmp", ".tga", ".hdr", ".ktx2"}))
            return ICON_MDI_IMAGE;
        if (hasExtension(path, {".vscn"}))
            return ICON_MDI_FILE_TREE;
        if (hasExtension(path, {".gltf", ".glb", ".obj", ".fbx", ".ply", ".spz"}))
            return ICON_MDI_CUBE_OUTLINE;
        if (hasExtension(path, {".lua"}))
            return ICON_MDI_LANGUAGE_LUA;
        if (hasExtension(path, {".h", ".hpp", ".c", ".cpp", ".glsl", ".vshader"}))
            return ICON_MDI_CODE_BRACES;
        return ICON_MDI_FILE_OUTLINE;
    }

    bool iconButton(const char* icon, const char* tip, const bool selected)
    {
        if (selected)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_Header));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered));
        }

        const bool pressed = ImGui::SmallButton(icon);
        tooltip(tip);

        if (selected)
            ImGui::PopStyleColor(2);
        return pressed;
    }

    bool toolbarToggle(const char* icon, const char* label, const bool selected)
    {
        const auto text = std::string(icon) + "  " + label;
        if (selected)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_Header));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered));
        }

        const bool pressed = ImGui::SmallButton(text.c_str());
        tooltip(label);

        if (selected)
            ImGui::PopStyleColor(2);
        return pressed;
    }

    void helpMarker(const char* text)
    {
        ImGui::TextDisabled("%s", ICON_MDI_HELP_CIRCLE_OUTLINE);
        tooltip(text);
    }

    void emptyState(const char* icon, const char* title, const char* message)
    {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const ImVec2 center {start.x + avail.x * 0.5f, start.y + avail.y * 0.5f};

        auto* drawList = ImGui::GetWindowDrawList();
        drawList->AddCircleFilled(center, 38.0f, IM_COL32(255, 255, 255, 14), 48);
        const ImVec2 iconSize = ImGui::CalcTextSize(icon);
        drawList->AddText(ImVec2(center.x - iconSize.x * 0.5f, center.y - 43.0f),
                          IM_COL32(205, 215, 230, 220),
                          icon);

        const ImVec2 titleSize = ImGui::CalcTextSize(title);
        drawList->AddText(ImVec2(center.x - titleSize.x * 0.5f, center.y + 10.0f),
                          IM_COL32(230, 235, 245, 240),
                          title);

        const ImVec2 msgSize = ImGui::CalcTextSize(message);
        drawList->AddText(ImVec2(center.x - msgSize.x * 0.5f, center.y + 32.0f),
                          IM_COL32(145, 155, 170, 230),
                          message);
    }

    void sectionTitle(const char* icon, const char* label)
    {
        ImGui::TextColored(ImVec4(0.72f, 0.80f, 0.92f, 1.0f), "%s", icon);
        ImGui::SameLine();
        ImGui::TextUnformatted(label);
        ImGui::Separator();
    }
} // namespace vultra_app::ui
