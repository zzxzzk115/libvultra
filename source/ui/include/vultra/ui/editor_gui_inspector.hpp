#pragma once

#include <optional>

namespace vultra
{
    class EditorGui;

    struct EditorGuiProperty
    {
        const char* id;
        const char* label;
    };

    // Values are borrowed for one draw call. Registered userData must outlive its EditorGui registration.
    struct EditorGuiPropertyDrawer
    {
        using Draw = bool (*)(EditorGuiProperty property, void* value, void* userData);
        Draw  draw;
        void* userData = nullptr;
    };

    // Immediate-mode property rows inside an ImGui window; never owns edited values.
    class EditorGuiInspector
    {
    public:
        EditorGuiInspector(EditorGui& gui, const char* id, float labelFraction = 0.45f);
        ~EditorGuiInspector();
        EditorGuiInspector(const EditorGuiInspector&)            = delete;
        EditorGuiInspector& operator=(const EditorGuiInspector&) = delete;

        explicit operator bool() const
        {
            return m_Visible;
        }

        bool boolField(EditorGuiProperty property, bool* value);
        bool floatSlider(EditorGuiProperty property, float* value, float min, float max);
        bool choice(EditorGuiProperty property, int* index, const char* const* items, int count);
        bool property(EditorGuiProperty property, void* value, EditorGuiPropertyDrawer drawer);

    private:
        void                beginRow(EditorGuiProperty property);
        std::optional<bool> drawCustom(EditorGuiProperty property, void* value);

        EditorGui& m_Gui;
        bool       m_Visible;
    };
} // namespace vultra
