#pragma once

#include <vultra/core/base/api_annotations.hpp>

#include <imgui.h>

#include <cstdarg>
#include <string>
#include <string_view>

namespace vultra
{
    class EditorGui;

    // Immediate-mode widgets for the current EditorGui frame. This value owns no ImGui state.
    class EditorGuiFrame
    {
    public:
        explicit EditorGuiFrame(EditorGui& gui);

        void   setNextWindowPos(ImVec2 position, ImGuiCond condition = 0);
        void   setNextWindowSize(ImVec2 size, ImGuiCond condition = 0);
        ImVec2 mainViewportPos() const;
        ImVec2 contentRegionAvail() const;

        bool beginWindow(const char* title, bool* open = nullptr, ImGuiWindowFlags flags = 0);
        void endWindow();
        bool beginChild(const char* id, ImVec2 size, ImGuiChildFlags flags = 0);
        void endChild();
        void text(const char* format, ...) IM_FMTARGS(2);
        void textWrapped(const char* format, ...) IM_FMTARGS(2);
        void textDisabled(const char* format, ...) IM_FMTARGS(2);
        void bulletText(const char* format, ...) IM_FMTARGS(2);
        void textUnformatted(const char* text);
        void separator();
        void separatorText(const char* text);
        bool button(const char* label);
        void sameLine();
        void image(ImTextureID texture, ImVec2 size);
        void showDemoWindow(bool* open);

        bool checkbox(const char* label, bool* value);
        bool inputText(const char* label, std::string* value);
        bool sliderDouble(const char* label, double* value, double min, double max);
        bool sliderFloat(const char* label, float* value, float min, float max, const char* format = "%.3f");
        bool sliderFloat3(const char* label, float* value, float min, float max);
        bool colorEdit3(const char* label, float* color);
        bool combo(const char* label, int* selected, const char* items);
        bool comboValue(int* selected, const char* items);
        bool beginCombo(const char* label, const char* preview);
        void endCombo();
        bool selectable(const char* label, bool selected);
        bool collapsingHeader(const char* label);
        bool treeNodeEx(const char* id, ImGuiTreeNodeFlags flags, const char* format, ...) IM_FMTARGS(4);
        void treePop();
        bool isItemClicked() const;
        void pushId(const char* id);
        void pushId(int id);
        void popId();
        void pushItemWidth(float width);
        void popItemWidth();
        void setNextItemWidth(float width);
        void beginDisabled(bool disabled = true);
        void endDisabled();

    private:
        EditorGui& m_Gui;
    };

    // Shared two-column property layout. Controls live in the right column.
    class EditorGuiLayout
    {
    public:
        static bool beginProperty(const char* label);
        static void endProperty();
    };

    // The annotated C++ surface is the sole source for generated UI ABI wrappers.
    VULTRA_BIND_UI bool guiButton(EditorGuiFrame& frame, std::string_view label);
    VULTRA_BIND_UI void guiText(EditorGuiFrame& frame, std::string_view text);

    class EditorGuiWindow
    {
    public:
        EditorGuiWindow(EditorGuiFrame& frame, const char* title, bool* open = nullptr, ImGuiWindowFlags flags = 0);
        ~EditorGuiWindow();
        EditorGuiWindow(const EditorGuiWindow&)            = delete;
        EditorGuiWindow& operator=(const EditorGuiWindow&) = delete;

        explicit operator bool() const
        {
            return m_Visible;
        }

    private:
        EditorGuiFrame& m_Frame;
        bool            m_Visible;
    };
} // namespace vultra
