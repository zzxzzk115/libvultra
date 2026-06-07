#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace vultra_app::ui
{
    // Hierarchical picker. `items` are '/'-delimited ids (e.g.
    // "builtin/highend/deferred_lighting.frag"); they are rendered as nested
    // submenus split on '/'. Call this inside an open popup / menu / combo body.
    // Returns true and writes the full id to `outSelected` when a leaf is
    // clicked. `currentSelection`, if given, is shown checked. When `filter` is
    // non-empty, matching leaves are shown flat (by full id) instead of nested.
    bool hierarchicalMenu(const std::vector<std::string>& items,
                          std::string&                    outSelected,
                          std::string_view                currentSelection = {},
                          std::string_view                filter           = {});

    // Convenience: a labelled combo whose dropdown body is a hierarchicalMenu.
    // `current` is shown as the preview (leaf name) and updated on selection.
    // Returns true when the selection changed.
    bool hierarchicalCombo(const char*                     label,
                           const std::vector<std::string>& items,
                           std::string&                    current);

    [[nodiscard]] const char* sourceAssetIcon(const std::filesystem::path& path, bool isDirectory);

    bool iconButton(const char* icon, const char* tooltip, bool selected = false);
    bool toolbarToggle(const char* icon, const char* label, bool selected);
    void helpMarker(const char* text);
    void emptyState(const char* icon, const char* title, const char* message);
    void sectionTitle(const char* icon, const char* label);
    void capturePreviewInput(bool hoveredOrActive);
    [[nodiscard]] bool capturePreviewItemInput();

    class ScopedPopupStyle
    {
    public:
        ScopedPopupStyle();
        ScopedPopupStyle(const ScopedPopupStyle&) = delete;
        ScopedPopupStyle& operator=(const ScopedPopupStyle&) = delete;
        ~ScopedPopupStyle();

    private:
        int m_ColorCount {0};
        int m_StyleVarCount {0};
    };
} // namespace vultra_app::ui
