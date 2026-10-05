#include <vultra/ui/editor_gui.hpp>
#include <vultra/ui/editor_gui_inspector.hpp>

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <cassert>
#include <cfloat>
#include <stdexcept>

namespace vultra
{
    namespace
    {
        struct PropertyIdScope
        {
            explicit PropertyIdScope(const char* id)
            {
                ImGui::PushID(id);
            }

            ~PropertyIdScope()
            {
                ImGui::PopID();
            }

            PropertyIdScope(const PropertyIdScope&)            = delete;
            PropertyIdScope& operator=(const PropertyIdScope&) = delete;
        };
    } // namespace

    EditorGuiInspector::EditorGuiInspector(EditorGui& gui, const char* id, float labelFraction) :
        m_Gui(gui),
        m_Visible(false)
    {
        if (!gui.frameActive())
        {
            throw std::logic_error("EditorGuiInspector requires an active GUI frame");
        }
        const float labelWidth = ImGui::GetContentRegionAvail().x * labelFraction;
        m_Visible              = ImGui::BeginTable(id, 2, ImGuiTableFlags_NoSavedSettings);
        if (m_Visible)
        {
            ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, labelWidth);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
        }
    }

    EditorGuiInspector::~EditorGuiInspector()
    {
        if (m_Visible)
        {
            ImGui::EndTable();
        }
    }

    void EditorGuiInspector::beginRow(EditorGuiProperty property)
    {
        assert(m_Visible);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(property.label);
        ImGui::TableSetColumnIndex(1);
    }

    bool EditorGuiInspector::boolField(EditorGuiProperty property, bool* value)
    {
        if (const auto custom = drawCustom(property, value))
        {
            return *custom;
        }
        beginRow(property);
        PropertyIdScope id(property.id);
        return ImGui::Checkbox("##value", value);
    }

    bool EditorGuiInspector::textField(EditorGuiProperty property, std::string* value)
    {
        if (const auto custom = drawCustom(property, value))
        {
            return *custom;
        }
        beginRow(property);
        PropertyIdScope id(property.id);
        ImGui::SetNextItemWidth(-FLT_MIN);
        return ImGui::InputText("##value", value);
    }

    bool EditorGuiInspector::floatField(EditorGuiProperty property, float* value, float speed, float min, float max)
    {
        if (const auto custom = drawCustom(property, value))
        {
            return *custom;
        }
        beginRow(property);
        PropertyIdScope id(property.id);
        ImGui::SetNextItemWidth(-FLT_MIN);
        return ImGui::DragFloat("##value", value, speed, min, max);
    }

    bool EditorGuiInspector::float3Field(EditorGuiProperty property, float* value, float speed)
    {
        if (const auto custom = drawCustom(property, value))
        {
            return *custom;
        }
        beginRow(property);
        PropertyIdScope id(property.id);
        ImGui::SetNextItemWidth(-FLT_MIN);
        return ImGui::DragFloat3("##value", value, speed);
    }

    bool EditorGuiInspector::floatSlider(EditorGuiProperty property, float* value, float min, float max)
    {
        if (const auto custom = drawCustom(property, value))
        {
            return *custom;
        }
        beginRow(property);
        PropertyIdScope id(property.id);
        ImGui::SetNextItemWidth(-FLT_MIN);
        return ImGui::SliderFloat("##value", value, min, max);
    }

    bool EditorGuiInspector::choice(EditorGuiProperty property, int* index, const char* const* items, int count)
    {
        if (const auto custom = drawCustom(property, index))
        {
            return *custom;
        }
        beginRow(property);
        PropertyIdScope id(property.id);
        ImGui::SetNextItemWidth(-FLT_MIN);
        return ImGui::Combo("##value", index, items, count);
    }

    bool EditorGuiInspector::property(EditorGuiProperty property, void* value, EditorGuiPropertyDrawer drawer)
    {
        if (const auto custom = drawCustom(property, value))
        {
            return *custom;
        }
        if (!drawer.draw)
        {
            throw std::invalid_argument("EditorGuiInspector property drawer is null");
        }
        beginRow(property);
        PropertyIdScope id(property.id);
        return drawer.draw(property, value, drawer.userData);
    }

    std::optional<bool> EditorGuiInspector::drawCustom(EditorGuiProperty property, void* value)
    {
        const auto drawer = m_Gui.propertyDrawer(property.id);
        if (!drawer)
        {
            return std::nullopt;
        }
        beginRow(property);
        PropertyIdScope id(property.id);
        return drawer->draw(property, value, drawer->userData);
    }
} // namespace vultra
