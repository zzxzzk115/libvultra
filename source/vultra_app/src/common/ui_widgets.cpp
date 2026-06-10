#include "common/ui_widgets.hpp"

#include <vultra/function/imgui/imgui_dpi.hpp>
#include <vultra/function/imgui/imgui_theme.hpp>

#include <IconsMaterialDesignIcons.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <map>
#include <string>

namespace vultra_app::ui
{
    namespace
    {
        bool hasExtension(const std::filesystem::path& path, std::initializer_list<const char*> exts)
        {
            auto ext = path.extension().generic_string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            return std::any_of(exts.begin(), exts.end(), [&](const char* candidate) { return ext == candidate; });
        }

        std::string lowerFileName(const std::filesystem::path& path)
        {
            auto name = path.filename().generic_string();
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            return name;
        }

        bool hasSuffix(const std::string& text, const std::string_view suffix)
        {
            return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
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
        if (hasExtension(path, {".vprefab"}))
            return ICON_MDI_CUBE;
        const auto name = lowerFileName(path);
        if (hasSuffix(name, ".vrg.json"))
            return ICON_MDI_GRAPH;
        if (hasSuffix(name, ".vmat.json"))
            return ICON_MDI_PALETTE_SWATCH;
        if (hasSuffix(name, ".vmatnode.json"))
            return ICON_MDI_PUZZLE;
        if (hasSuffix(name, ".vmatgraph.json") || hasExtension(path, {".vmatgraph"}))
            return ICON_MDI_PALETTE;
        if (hasSuffix(name, ".vanimgraph.json") || hasExtension(path, {".vanimgraph"}))
            return ICON_MDI_RUN_FAST;
        if (hasExtension(path, {".gltf", ".glb", ".obj", ".fbx", ".dae", ".ply", ".spz"}))
            return ICON_MDI_CUBE_OUTLINE;
        if (hasExtension(path, {".lua"}))
            return ICON_MDI_LANGUAGE_LUA;
        if (hasSuffix(name, ".vshaderlib.lua"))
            return ICON_MDI_SOURCE_BRANCH;
        if (hasSuffix(name, ".vfeature.lua") || hasSuffix(name, ".vsrp.lua") || hasSuffix(name, ".vso.lua"))
            return ICON_MDI_FUNCTION;
        if (hasExtension(path, {".glsl", ".vert", ".frag", ".comp", ".hlsl", ".vshader"}))
            return ICON_MDI_ATOM;
        if (hasExtension(path, {".h", ".hpp", ".c", ".cpp"}))
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

        auto* drawList  = ImGui::GetWindowDrawList();
        namespace theme = vultra::imgui_theme;
        drawList->AddCircleFilled(
            center, vultra::ui::dp(38.0f), theme::u32(theme::withAlpha(theme::accent(), 14.0f / 255.0f)), 48);
        const ImVec2 iconSize = ImGui::CalcTextSize(icon);
        drawList->AddText(ImVec2(center.x - iconSize.x * 0.5f, center.y - vultra::ui::dp(43.0f)),
                          theme::u32(theme::withAlpha(theme::textSoft(), 220.0f / 255.0f)),
                          icon);

        const ImVec2 titleSize = ImGui::CalcTextSize(title);
        drawList->AddText(ImVec2(center.x - titleSize.x * 0.5f, center.y + vultra::ui::dp(10.0f)),
                          theme::u32(theme::withAlpha(theme::text(), 240.0f / 255.0f)),
                          title);

        const ImVec2 msgSize = ImGui::CalcTextSize(message);
        drawList->AddText(ImVec2(center.x - msgSize.x * 0.5f, center.y + vultra::ui::dp(32.0f)),
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

    void capturePreviewInput(const bool hoveredOrActive)
    {
        if (!hoveredOrActive)
            return;

        const auto id = ImGui::GetCurrentWindow() ? ImGui::GetCurrentWindow()->ID : ImGui::GetID("PreviewInput");
        ImGui::SetKeyOwner(ImGuiKey_MouseWheelX, id);
        ImGui::SetKeyOwner(ImGuiKey_MouseWheelY, id);
        ImGui::SetNextFrameWantCaptureMouse(true);
    }

    bool capturePreviewItemInput()
    {
        const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        if (hovered)
        {
            ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelX);
            ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
        }
        capturePreviewInput(hovered);
        return hovered;
    }

    ScopedPopupStyle::ScopedPopupStyle()
    {
        namespace theme = vultra::imgui_theme;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, vultra::ui::dp(6.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, vultra::ui::dp(4.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, vultra::ui::dp(4.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, vultra::ui::dp(6.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, vultra::ui::dp(4.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, vultra::ui::dp(4.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2 {vultra::ui::dp(14.0f), vultra::ui::dp(12.0f)});
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2 {vultra::ui::dp(8.0f), vultra::ui::dp(5.0f)});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2 {vultra::ui::dp(7.0f), vultra::ui::dp(6.0f)});
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

    namespace
    {
        // A node in the '/'-split menu tree. Interior nodes hold ordered
        // children; a leaf carries the full id it was built from.
        struct MenuNode
        {
            std::map<std::string, MenuNode> children;
            std::string                     fullId;
            bool                            leaf {false};
        };

        void insertMenuPath(MenuNode& root, const std::string& id)
        {
            MenuNode* node = &root;
            size_t    start = 0;
            while (start <= id.size())
            {
                const auto slash   = id.find('/', start);
                const auto segment = id.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
                if (!segment.empty())
                    node = &node->children[segment];
                if (slash == std::string::npos)
                    break;
                start = slash + 1;
            }
            node->leaf   = true;
            node->fullId = id;
        }

        bool drawMenuNode(const MenuNode& node, std::string& outSelected, std::string_view current)
        {
            bool picked = false;
            for (const auto& [name, child] : node.children)
            {
                if (child.children.empty())
                {
                    const bool selected = !current.empty() && child.fullId == current;
                    if (ImGui::MenuItem(name.c_str(), nullptr, selected))
                    {
                        outSelected = child.fullId;
                        picked      = true;
                    }
                }
                else if (ImGui::BeginMenu(name.c_str()))
                {
                    // A node that is itself a leaf and also a parent: offer it first.
                    if (child.leaf)
                    {
                        const bool selected = !current.empty() && child.fullId == current;
                        if (ImGui::MenuItem(name.c_str(), nullptr, selected))
                        {
                            outSelected = child.fullId;
                            picked      = true;
                        }
                        ImGui::Separator();
                    }
                    if (drawMenuNode(child, outSelected, current))
                        picked = true;
                    ImGui::EndMenu();
                }
            }
            return picked;
        }

        std::string lowerCopy(std::string_view s)
        {
            std::string out {s};
            std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return out;
        }

        std::string_view leafName(std::string_view id)
        {
            const auto slash = id.rfind('/');
            return slash == std::string_view::npos ? id : id.substr(slash + 1);
        }
    } // namespace

    bool hierarchicalMenu(const std::vector<std::string>& items,
                          std::string&                    outSelected,
                          std::string_view                currentSelection,
                          std::string_view                filter)
    {
        if (!filter.empty())
        {
            // Flat, filtered view: match anywhere in the full id (case-insensitive).
            const auto needle = lowerCopy(filter);
            bool       picked = false;
            for (const auto& id : items)
            {
                if (lowerCopy(id).find(needle) == std::string::npos)
                    continue;
                const bool selected = !currentSelection.empty() && id == currentSelection;
                if (ImGui::MenuItem(id.c_str(), nullptr, selected))
                {
                    outSelected = id;
                    picked      = true;
                }
            }
            return picked;
        }

        MenuNode root;
        for (const auto& id : items)
            insertMenuPath(root, id);
        return drawMenuNode(root, outSelected, currentSelection);
    }

    bool hierarchicalCombo(const char* label, const std::vector<std::string>& items, std::string& current)
    {
        bool       changed = false;
        const auto preview  = current.empty() ? std::string {"<none>"} : std::string {leafName(current)};
        if (ImGui::BeginCombo(label, preview.c_str()))
        {
            std::string selected;
            if (hierarchicalMenu(items, selected, current) && selected != current)
            {
                current = std::move(selected);
                changed = true;
            }
            ImGui::EndCombo();
        }
        return changed;
    }
} // namespace vultra_app::ui
