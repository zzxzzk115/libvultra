#include <vultra/ui/editor_gui.hpp>
#include <vultra/ui/editor_gui_frame.hpp>

#include <misc/cpp/imgui_stdlib.h>

#include <cfloat>
#include <cmath>
#include <stdexcept>
#include <string>

namespace vultra
{
    EditorGuiFrame::EditorGuiFrame(EditorGui& gui) :
        m_Gui(gui),
        m_Serial(gui.frameSerial())
    {
        requireActive();
    }

    void EditorGuiFrame::requireActive() const
    {
        m_Gui.requireFrame(m_Serial);
    }

    void EditorGuiFrame::setNextWindowPos(ImVec2 position, ImGuiCond condition)
    {
        requireActive();
        ImGui::SetNextWindowPos(position, condition);
    }

    void EditorGuiFrame::setNextWindowSize(ImVec2 size, ImGuiCond condition)
    {
        requireActive();
        ImGui::SetNextWindowSize(size, condition);
    }

    ImVec2 EditorGuiFrame::mainViewportPos() const
    {
        requireActive();
        return ImGui::GetMainViewport()->Pos;
    }

    ImVec2 EditorGuiFrame::contentRegionAvail() const
    {
        requireActive();
        return ImGui::GetContentRegionAvail();
    }

    bool EditorGuiFrame::beginWindow(const char* title, bool* open, ImGuiWindowFlags flags)
    {
        requireActive();
        return ImGui::Begin(title, open, flags);
    }

    void EditorGuiFrame::endWindow()
    {
        requireActive();
        ImGui::End();
    }

    bool EditorGuiFrame::beginChild(const char* id, ImVec2 size, ImGuiChildFlags flags)
    {
        requireActive();
        return ImGui::BeginChild(id, size, flags);
    }

    void EditorGuiFrame::endChild()
    {
        requireActive();
        ImGui::EndChild();
    }

    void EditorGuiFrame::text(const char* format, ...)
    {
        requireActive();
        va_list args;
        va_start(args, format);
        ImGui::TextV(format, args);
        va_end(args);
    }

    void EditorGuiFrame::textWrapped(const char* format, ...)
    {
        requireActive();
        va_list args;
        va_start(args, format);
        ImGui::TextWrappedV(format, args);
        va_end(args);
    }

    void EditorGuiFrame::textDisabled(const char* format, ...)
    {
        requireActive();
        va_list args;
        va_start(args, format);
        ImGui::TextDisabledV(format, args);
        va_end(args);
    }

    void EditorGuiFrame::bulletText(const char* format, ...)
    {
        requireActive();
        va_list args;
        va_start(args, format);
        ImGui::BulletTextV(format, args);
        va_end(args);
    }

    void EditorGuiFrame::textUnformatted(const char* value)
    {
        requireActive();
        ImGui::TextUnformatted(value);
    }

    void EditorGuiFrame::separator()
    {
        requireActive();
        ImGui::Separator();
    }

    void EditorGuiFrame::separatorText(const char* value)
    {
        requireActive();
        ImGui::SeparatorText(value);
    }

    bool EditorGuiFrame::button(const char* label)
    {
        requireActive();
        return ImGui::Button(label);
    }

    void EditorGuiFrame::sameLine()
    {
        requireActive();
        ImGui::SameLine();
    }

    void EditorGuiFrame::image(ImTextureID texture, ImVec2 size)
    {
        requireActive();
        ImGui::Image(texture, size);
    }

    void EditorGuiFrame::showDemoWindow(bool* open)
    {
        requireActive();
        ImGui::ShowDemoWindow(open);
    }

    bool EditorGuiLayout::beginProperty(const char* label)
    {
        ImGui::PushID(label);
        if (!ImGui::BeginTable("##property", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoPadOuterX))
        {
            ImGui::PopID();
            return false;
        }
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch, 0.45f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.55f);
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted(label);
        ImGui::PopTextWrapPos();
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        return true;
    }

    void EditorGuiLayout::endProperty()
    {
        ImGui::EndTable();
        ImGui::PopID();
    }

    bool EditorGuiFrame::checkbox(const char* label, bool* value)
    {
        requireActive();
        if (!EditorGuiLayout::beginProperty(label))
        {
            return false;
        }
        const bool changed = ImGui::Checkbox("##value", value);
        EditorGuiLayout::endProperty();
        return changed;
    }

    bool EditorGuiFrame::sliderFloat(const char* label, float* value, float min, float max, const char* format)
    {
        requireActive();
        if (!EditorGuiLayout::beginProperty(label))
        {
            return false;
        }
        const bool changed = ImGui::SliderFloat("##value", value, min, max, format);
        EditorGuiLayout::endProperty();
        return changed;
    }

    bool EditorGuiFrame::inputText(const char* label, std::string* value)
    {
        requireActive();
        if (!EditorGuiLayout::beginProperty(label))
        {
            return false;
        }
        const bool changed = ImGui::InputText("##value", value);
        EditorGuiLayout::endProperty();
        return changed;
    }

    bool EditorGuiFrame::sliderDouble(const char* label, double* value, double min, double max)
    {
        requireActive();
        if (!EditorGuiLayout::beginProperty(label))
        {
            return false;
        }
        const bool changed = ImGui::SliderScalar("##value",
                                                 ImGuiDataType_Double,
                                                 value,
                                                 &min,
                                                 &max,
                                                 "%.4g",
                                                 ImGuiSliderFlags_AlwaysClamp);
        EditorGuiLayout::endProperty();
        return changed;
    }

    bool EditorGuiFrame::sliderFloat3(const char* label, float* value, float min, float max)
    {
        requireActive();
        if (!EditorGuiLayout::beginProperty(label))
        {
            return false;
        }
        const bool changed = ImGui::SliderFloat3("##value", value, min, max);
        EditorGuiLayout::endProperty();
        return changed;
    }

    bool EditorGuiFrame::colorEdit3(const char* label, float* color)
    {
        requireActive();
        if (!EditorGuiLayout::beginProperty(label))
        {
            return false;
        }
        const bool changed = ImGui::ColorEdit3("##value", color);
        EditorGuiLayout::endProperty();
        return changed;
    }

    bool EditorGuiFrame::combo(const char* label, int* selected, const char* items)
    {
        requireActive();
        if (!EditorGuiLayout::beginProperty(label))
        {
            return false;
        }
        const bool changed = ImGui::Combo("##value", selected, items);
        EditorGuiLayout::endProperty();
        return changed;
    }

    bool EditorGuiFrame::comboValue(int* selected, const char* items)
    {
        requireActive();
        return ImGui::Combo("##value", selected, items);
    }

    bool EditorGuiFrame::beginCombo(const char* label, const char* preview)
    {
        requireActive();
        if (!EditorGuiLayout::beginProperty(label))
        {
            return false;
        }
        if (ImGui::BeginCombo("##value", preview))
        {
            return true;
        }
        EditorGuiLayout::endProperty();
        return false;
    }

    void EditorGuiFrame::endCombo()
    {
        requireActive();
        ImGui::EndCombo();
        EditorGuiLayout::endProperty();
    }

    bool EditorGuiFrame::selectable(const char* label, bool selected)
    {
        requireActive();
        return ImGui::Selectable(label, selected);
    }

    bool EditorGuiFrame::collapsingHeader(const char* label)
    {
        requireActive();
        return ImGui::CollapsingHeader(label);
    }

    bool EditorGuiFrame::treeNodeEx(const char* id, ImGuiTreeNodeFlags flags, const char* format, ...)
    {
        requireActive();
        va_list args;
        va_start(args, format);
        const bool open = ImGui::TreeNodeExV(id, flags, format, args);
        va_end(args);
        return open;
    }

    bool EditorGuiFrame::isItemClicked() const
    {
        requireActive();
        return ImGui::IsItemClicked();
    }

    void EditorGuiFrame::treePop()
    {
        requireActive();
        ImGui::TreePop();
    }

    void EditorGuiFrame::pushId(const char* id)
    {
        requireActive();
        ImGui::PushID(id);
    }

    void EditorGuiFrame::pushId(int id)
    {
        requireActive();
        ImGui::PushID(id);
    }

    void EditorGuiFrame::popId()
    {
        requireActive();
        ImGui::PopID();
    }

    void EditorGuiFrame::pushItemWidth(float width)
    {
        requireActive();
        ImGui::PushItemWidth(width);
    }

    void EditorGuiFrame::popItemWidth()
    {
        requireActive();
        ImGui::PopItemWidth();
    }

    void EditorGuiFrame::setNextItemWidth(float width)
    {
        requireActive();
        ImGui::SetNextItemWidth(width);
    }

    void EditorGuiFrame::beginDisabled(bool disabled)
    {
        requireActive();
        ImGui::BeginDisabled(disabled);
    }

    void EditorGuiFrame::endDisabled()
    {
        requireActive();
        ImGui::EndDisabled();
    }

    EditorGuiWindow::EditorGuiWindow(EditorGuiFrame& frame, const char* title, bool* open, ImGuiWindowFlags flags) :
        m_Frame(frame),
        m_Visible(frame.beginWindow(title, open, flags))
    {
    }

    EditorGuiWindow::~EditorGuiWindow()
    {
        m_Frame.endWindow();
    }
} // namespace vultra
