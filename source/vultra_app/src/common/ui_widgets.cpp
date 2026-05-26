#include "common/ui_widgets.hpp"

#include <vultra/function/imgui/imgui_theme.hpp>

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
        if (hasExtension(path, {".gltf", ".glb", ".obj", ".fbx", ".dae", ".ply", ".spz"}))
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
        namespace theme = vultra::imgui_theme;
        drawList->AddCircleFilled(center, 38.0f, theme::u32(theme::withAlpha(theme::accent(), 14.0f / 255.0f)), 48);
        const ImVec2 iconSize = ImGui::CalcTextSize(icon);
        drawList->AddText(ImVec2(center.x - iconSize.x * 0.5f, center.y - 43.0f),
                          theme::u32(theme::withAlpha(theme::textSoft(), 220.0f / 255.0f)),
                          icon);

        const ImVec2 titleSize = ImGui::CalcTextSize(title);
        drawList->AddText(ImVec2(center.x - titleSize.x * 0.5f, center.y + 10.0f),
                          theme::u32(theme::withAlpha(theme::text(), 240.0f / 255.0f)),
                          title);

        const ImVec2 msgSize = ImGui::CalcTextSize(message);
        drawList->AddText(ImVec2(center.x - msgSize.x * 0.5f, center.y + 32.0f),
                          theme::u32(theme::withAlpha(theme::textMuted(), 230.0f / 255.0f)),
                          message);
    }

    void sectionTitle(const char* icon, const char* label)
    {
        ImGui::TextColored(vultra::imgui_theme::textSoft(), "%s", icon);
        ImGui::SameLine();
        ImGui::TextUnformatted(label);
        ImGui::Separator();
    }

    ScopedPopupStyle::ScopedPopupStyle()
    {
        namespace theme = vultra::imgui_theme;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2 {14.0f, 12.0f});
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2 {8.0f, 5.0f});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2 {7.0f, 6.0f});
        m_StyleVarCount = 9;

        ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::background());
        ImGui::PushStyleColor(ImGuiCol_PopupBg, theme::background());
        ImGui::PushStyleColor(ImGuiCol_Border, theme::border());
        ImGui::PushStyleColor(ImGuiCol_TitleBg, theme::backgroundDeep());
        ImGui::PushStyleColor(ImGuiCol_TitleBgActive, theme::panel());
        ImGui::PushStyleColor(ImGuiCol_FrameBg, theme::frame());
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, theme::frameHovered());
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, theme::frameActive());
        ImGui::PushStyleColor(ImGuiCol_Button, theme::button());
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::buttonHovered());
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme::accentButton());
        ImGui::PushStyleColor(ImGuiCol_Header, theme::header());
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, theme::headerHovered());
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, theme::headerActive());
        ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, ImVec4 {0.080f, 0.100f, 0.128f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, ImVec4 {1.0f, 1.0f, 1.0f, 0.025f});
        ImGui::PushStyleColor(ImGuiCol_CheckMark, theme::accent());
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4 {0.320f, 0.600f, 0.880f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, ImVec4 {0.110f, 0.380f, 0.660f, 0.55f});
        m_ColorCount = 19;
    }

    ScopedPopupStyle::~ScopedPopupStyle()
    {
        ImGui::PopStyleColor(m_ColorCount);
        ImGui::PopStyleVar(m_StyleVarCount);
    }
} // namespace vultra_app::ui
