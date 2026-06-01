#pragma once

#include <filesystem>

namespace vultra_app::ui
{
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
