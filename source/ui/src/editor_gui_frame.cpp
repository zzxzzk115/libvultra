#include <vultra/ui/editor_gui.hpp>
#include <vultra/ui/editor_gui_frame.hpp>

#include <cfloat>
#include <stdexcept>
#include <string>

namespace vultra
{
    EditorGuiFrame::EditorGuiFrame(EditorGui& gui) :
        m_Gui(gui)
    {
        if (!m_Gui.frameActive())
        {
            throw std::logic_error("GUI widgets require an active EditorGui frame");
        }
    }

    void EditorGuiFrame::setNextWindowPos(ImVec2 position, ImGuiCond condition)
    {
        ImGui::SetNextWindowPos(position, condition);
    }

    void EditorGuiFrame::setNextWindowSize(ImVec2 size, ImGuiCond condition)
    {
        ImGui::SetNextWindowSize(size, condition);
    }

    ImVec2 EditorGuiFrame::mainViewportPos() const
    {
        return ImGui::GetMainViewport()->Pos;
    }

    ImVec2 EditorGuiFrame::contentRegionAvail() const
    {
        return ImGui::GetContentRegionAvail();
    }

    bool EditorGuiFrame::beginWindow(const char* title, bool* open, ImGuiWindowFlags flags)
    {
        return ImGui::Begin(title, open, flags);
    }

    void EditorGuiFrame::endWindow()
    {
        ImGui::End();
    }

    void EditorGuiFrame::text(const char* format, ...)
    {
        va_list args;
        va_start(args, format);
        ImGui::TextV(format, args);
        va_end(args);
    }

    void EditorGuiFrame::textWrapped(const char* format, ...)
    {
        va_list args;
        va_start(args, format);
        ImGui::TextWrappedV(format, args);
        va_end(args);
    }

    void EditorGuiFrame::textDisabled(const char* format, ...)
    {
        va_list args;
        va_start(args, format);
        ImGui::TextDisabledV(format, args);
        va_end(args);
    }

    void EditorGuiFrame::bulletText(const char* format, ...)
    {
        va_list args;
        va_start(args, format);
        ImGui::BulletTextV(format, args);
        va_end(args);
    }

    void EditorGuiFrame::textUnformatted(const char* value)
    {
        ImGui::TextUnformatted(value);
    }

    void EditorGuiFrame::separator()
    {
        ImGui::Separator();
    }

    void EditorGuiFrame::separatorText(const char* value)
    {
        ImGui::SeparatorText(value);
    }

    bool EditorGuiFrame::button(const char* label)
    {
        return ImGui::Button(label);
    }

    void EditorGuiFrame::sameLine()
    {
        ImGui::SameLine();
    }

    void EditorGuiFrame::image(ImTextureID texture, ImVec2 size)
    {
        ImGui::Image(texture, size);
    }

    void EditorGuiFrame::showDemoWindow(bool* open)
    {
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
        ImGui::TextUnformatted(label);
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
        if (!EditorGuiLayout::beginProperty(label))
        {
            return false;
        }
        const bool changed = ImGui::SliderFloat("##value", value, min, max, format);
        EditorGuiLayout::endProperty();
        return changed;
    }

    bool EditorGuiFrame::sliderFloat3(const char* label, float* value, float min, float max)
    {
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
        return ImGui::Combo("##value", selected, items);
    }

    bool EditorGuiFrame::beginCombo(const char* label, const char* preview)
    {
        return ImGui::BeginCombo(label, preview);
    }

    void EditorGuiFrame::endCombo()
    {
        ImGui::EndCombo();
    }

    bool EditorGuiFrame::selectable(const char* label, bool selected)
    {
        return ImGui::Selectable(label, selected);
    }

    bool EditorGuiFrame::collapsingHeader(const char* label)
    {
        return ImGui::CollapsingHeader(label);
    }

    bool EditorGuiFrame::treeNodeEx(const char* id, ImGuiTreeNodeFlags flags, const char* format, ...)
    {
        va_list args;
        va_start(args, format);
        const bool open = ImGui::TreeNodeExV(id, flags, format, args);
        va_end(args);
        return open;
    }

    void EditorGuiFrame::treePop()
    {
        ImGui::TreePop();
    }

    void EditorGuiFrame::pushId(int id)
    {
        ImGui::PushID(id);
    }

    void EditorGuiFrame::popId()
    {
        ImGui::PopID();
    }

    void EditorGuiFrame::pushItemWidth(float width)
    {
        ImGui::PushItemWidth(width);
    }

    void EditorGuiFrame::popItemWidth()
    {
        ImGui::PopItemWidth();
    }

    void EditorGuiFrame::setNextItemWidth(float width)
    {
        ImGui::SetNextItemWidth(width);
    }

    void EditorGuiFrame::beginDisabled(bool disabled)
    {
        ImGui::BeginDisabled(disabled);
    }

    void EditorGuiFrame::endDisabled()
    {
        ImGui::EndDisabled();
    }

    bool guiButton(EditorGuiFrame& frame, std::string_view label)
    {
        return frame.button(std::string(label).c_str());
    }

    void guiText(EditorGuiFrame&, std::string_view text)
    {
        ImGui::TextUnformatted(text.data(), text.data() + text.size());
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
