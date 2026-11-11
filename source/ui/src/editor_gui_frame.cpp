#include <vultra/ui/editor_gui.hpp>
#include <vultra/ui/editor_gui_frame.hpp>

#include <misc/cpp/imgui_stdlib.h>

#include <cfloat>
#include <cmath>
#include <string>

namespace vultra
{
    EditorGuiFrame::EditorGuiFrame(EditorGui& gui) :
        m_Gui(gui),
        m_Serial(gui.frameSerial())
    {
        m_Gui.requireFrame(m_Serial);
    }

    std::expected<void, Error> EditorGuiFrame::requireActive() const
    {
        return m_Gui.checkFrame(m_Serial);
    }

    void EditorGuiFrame::setNextWindowPos(ImVec2 position, ImGuiCond condition)
    {
        m_Gui.requireFrame(m_Serial);
        ImGui::SetNextWindowPos(position, condition);
    }

    void EditorGuiFrame::setNextWindowSize(ImVec2 size, ImGuiCond condition)
    {
        m_Gui.requireFrame(m_Serial);
        ImGui::SetNextWindowSize(size, condition);
    }

    ImVec2 EditorGuiFrame::mainViewportPos() const
    {
        m_Gui.requireFrame(m_Serial);
        return ImGui::GetMainViewport()->Pos;
    }

    ImVec2 EditorGuiFrame::contentRegionAvail() const
    {
        m_Gui.requireFrame(m_Serial);
        return ImGui::GetContentRegionAvail();
    }

    bool EditorGuiFrame::beginWindow(const char* title, bool* open, ImGuiWindowFlags flags)
    {
        m_Gui.requireFrame(m_Serial);
        return ImGui::Begin(title, open, flags);
    }

    void EditorGuiFrame::endWindow()
    {
        m_Gui.requireFrame(m_Serial);
        ImGui::End();
    }

    bool EditorGuiFrame::beginChild(const char* id, ImVec2 size, ImGuiChildFlags flags)
    {
        m_Gui.requireFrame(m_Serial);
        return ImGui::BeginChild(id, size, flags);
    }

    void EditorGuiFrame::endChild()
    {
        m_Gui.requireFrame(m_Serial);
        ImGui::EndChild();
    }

    void EditorGuiFrame::text(const char* format, ...)
    {
        m_Gui.requireFrame(m_Serial);
        va_list args;
        va_start(args, format);
        ImGui::TextV(format, args);
        va_end(args);
    }

    void EditorGuiFrame::textWrapped(const char* format, ...)
    {
        m_Gui.requireFrame(m_Serial);
        va_list args;
        va_start(args, format);
        ImGui::TextWrappedV(format, args);
        va_end(args);
    }

    void EditorGuiFrame::textDisabled(const char* format, ...)
    {
        m_Gui.requireFrame(m_Serial);
        va_list args;
        va_start(args, format);
        ImGui::TextDisabledV(format, args);
        va_end(args);
    }

    void EditorGuiFrame::bulletText(const char* format, ...)
    {
        m_Gui.requireFrame(m_Serial);
        va_list args;
        va_start(args, format);
        ImGui::BulletTextV(format, args);
        va_end(args);
    }

    void EditorGuiFrame::textUnformatted(const char* value)
    {
        m_Gui.requireFrame(m_Serial);
        ImGui::TextUnformatted(value);
    }

    void EditorGuiFrame::separator()
    {
        m_Gui.requireFrame(m_Serial);
        ImGui::Separator();
    }

    void EditorGuiFrame::separatorText(const char* value)
    {
        m_Gui.requireFrame(m_Serial);
        ImGui::SeparatorText(value);
    }

    bool EditorGuiFrame::button(const char* label)
    {
        m_Gui.requireFrame(m_Serial);
        return ImGui::Button(label);
    }

    void EditorGuiFrame::sameLine()
    {
        m_Gui.requireFrame(m_Serial);
        ImGui::SameLine();
    }

    void EditorGuiFrame::image(ImTextureID texture, ImVec2 size)
    {
        m_Gui.requireFrame(m_Serial);
        ImGui::Image(texture, size);
    }

    void EditorGuiFrame::showDemoWindow(bool* open)
    {
        m_Gui.requireFrame(m_Serial);
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
        m_Gui.requireFrame(m_Serial);
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
        m_Gui.requireFrame(m_Serial);
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
        m_Gui.requireFrame(m_Serial);
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
        m_Gui.requireFrame(m_Serial);
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
        m_Gui.requireFrame(m_Serial);
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
        m_Gui.requireFrame(m_Serial);
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
        m_Gui.requireFrame(m_Serial);
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
        m_Gui.requireFrame(m_Serial);
        return ImGui::Combo("##value", selected, items);
    }

    bool EditorGuiFrame::beginCombo(const char* label, const char* preview)
    {
        m_Gui.requireFrame(m_Serial);
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
        m_Gui.requireFrame(m_Serial);
        ImGui::EndCombo();
        EditorGuiLayout::endProperty();
    }

    bool EditorGuiFrame::selectable(const char* label, bool selected)
    {
        m_Gui.requireFrame(m_Serial);
        return ImGui::Selectable(label, selected);
    }

    bool EditorGuiFrame::collapsingHeader(const char* label)
    {
        m_Gui.requireFrame(m_Serial);
        return ImGui::CollapsingHeader(label);
    }

    bool EditorGuiFrame::treeNodeEx(const char* id, ImGuiTreeNodeFlags flags, const char* format, ...)
    {
        m_Gui.requireFrame(m_Serial);
        va_list args;
        va_start(args, format);
        const bool open = ImGui::TreeNodeExV(id, flags, format, args);
        va_end(args);
        return open;
    }

    bool EditorGuiFrame::isItemClicked() const
    {
        m_Gui.requireFrame(m_Serial);
        return ImGui::IsItemClicked();
    }

    void EditorGuiFrame::treePop()
    {
        m_Gui.requireFrame(m_Serial);
        ImGui::TreePop();
    }

    void EditorGuiFrame::pushId(const char* id)
    {
        m_Gui.requireFrame(m_Serial);
        ImGui::PushID(id);
    }

    void EditorGuiFrame::pushId(int id)
    {
        m_Gui.requireFrame(m_Serial);
        ImGui::PushID(id);
    }

    void EditorGuiFrame::popId()
    {
        m_Gui.requireFrame(m_Serial);
        ImGui::PopID();
    }

    void EditorGuiFrame::pushItemWidth(float width)
    {
        m_Gui.requireFrame(m_Serial);
        ImGui::PushItemWidth(width);
    }

    void EditorGuiFrame::popItemWidth()
    {
        m_Gui.requireFrame(m_Serial);
        ImGui::PopItemWidth();
    }

    void EditorGuiFrame::setNextItemWidth(float width)
    {
        m_Gui.requireFrame(m_Serial);
        ImGui::SetNextItemWidth(width);
    }

    void EditorGuiFrame::beginDisabled(bool disabled)
    {
        m_Gui.requireFrame(m_Serial);
        ImGui::BeginDisabled(disabled);
    }

    void EditorGuiFrame::endDisabled()
    {
        m_Gui.requireFrame(m_Serial);
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
