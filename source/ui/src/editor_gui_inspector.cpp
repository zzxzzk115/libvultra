#include <vultra/ui/editor_gui.hpp>
#include <vultra/ui/editor_gui_inspector.hpp>

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
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

    bool EditorGuiInspector::properties(const ObjectTypeInfo& type, void* object)
    {
        bool changed = false;
        for (const auto& property : type.properties)
        {
            if (!hasPropertyFlag(property.flags, PropertyFlags::eInspect))
            {
                continue;
            }
            auto value = property.read(object);
            if (drawValue(property, value))
            {
                property.write(object, value);
                changed = true;
            }
        }
        return changed;
    }

    bool EditorGuiInspector::drawValue(const PropertyInfo& info, PropertyValue& value)
    {
        const EditorGuiProperty property {info.drawerId, info.label};
        if (info.kind == PropertyKind::eBool)
        {
            return boolField(property, &std::get<bool>(value));
        }
        if (info.kind == PropertyKind::eString)
        {
            return textField(property, &std::get<std::string>(value));
        }
        if (info.kind == PropertyKind::eFloat)
        {
            auto& number = std::get<float>(value);
            if (info.widget == PropertyWidget::eDrag)
            {
                return floatField(property, &number, info.speed, float(info.min), float(info.max));
            }
            return floatSlider(property, &number, float(info.min), float(info.max));
        }
        if (info.kind == PropertyKind::eEnum)
        {
            auto& enumeration = std::get<PropertyEnum>(value);
            int   selected    = int(enumeration.value);
            // Existing enum drawers receive an integer, while descriptors retain the actual enum values.
            if (const auto custom = drawCustom(property, &selected))
            {
                if (*custom)
                {
                    enumeration.value = selected;
                }
                return *custom;
            }
            beginRow(property);
            PropertyIdScope id(property.id);
            ImGui::SetNextItemWidth(-FLT_MIN);
            const auto choice  = std::ranges::find(info.choices, enumeration.value, &PropertyChoice::value);
            bool       changed = false;
            if (ImGui::BeginCombo("##value", choice == info.choices.end() ? "" : choice->label))
            {
                for (const auto& option : info.choices)
                {
                    if (ImGui::Selectable(option.label, enumeration.value == option.value))
                    {
                        enumeration.value = option.value;
                        changed           = true;
                    }
                }
                ImGui::EndCombo();
            }
            return changed;
        }
        const auto custom = std::visit(
            [&](auto& item)
            {
                return drawCustom(property, &item);
            },
            value);
        if (custom)
        {
            return *custom;
        }
        beginRow(property);
        PropertyIdScope id(property.id);
        ImGui::SetNextItemWidth(-FLT_MIN);
        switch (info.kind)
        {
            case PropertyKind::eInt: {
                const auto minimum = int64_t(info.min);
                const auto maximum = int64_t(info.max);
                auto&      number  = std::get<int64_t>(value);
                if (info.widget == PropertyWidget::eDrag)
                {
                    return ImGui::DragScalar("##value", ImGuiDataType_S64, &number, info.speed, &minimum, &maximum);
                }
                return ImGui::SliderScalar("##value", ImGuiDataType_S64, &number, &minimum, &maximum);
            }
            case PropertyKind::eUInt: {
                const auto minimum = uint64_t(info.min);
                const auto maximum = uint64_t(info.max);
                auto&      number  = std::get<uint64_t>(value);
                if (info.widget == PropertyWidget::eDrag)
                {
                    return ImGui::DragScalar("##value", ImGuiDataType_U64, &number, info.speed, &minimum, &maximum);
                }
                return ImGui::SliderScalar("##value", ImGuiDataType_U64, &number, &minimum, &maximum);
            }
            case PropertyKind::eDouble: {
                auto& number = std::get<double>(value);
                if (info.widget == PropertyWidget::eDrag)
                {
                    return ImGui::DragScalar("##value",
                                             ImGuiDataType_Double,
                                             &number,
                                             info.speed,
                                             &info.min,
                                             &info.max);
                }
                return ImGui::SliderScalar("##value", ImGuiDataType_Double, &number, &info.min, &info.max);
            }
            case PropertyKind::eVector2:
                return ImGui::DragFloat2("##value",
                                         &std::get<glm::vec2>(value)[0],
                                         info.speed,
                                         float(info.min),
                                         float(info.max));
            case PropertyKind::eVector3:
                return ImGui::DragFloat3("##value",
                                         &std::get<glm::vec3>(value)[0],
                                         info.speed,
                                         float(info.min),
                                         float(info.max));
            case PropertyKind::eVector4:
                return ImGui::DragFloat4("##value",
                                         &std::get<glm::vec4>(value)[0],
                                         info.speed,
                                         float(info.min),
                                         float(info.max));
            case PropertyKind::eMatrix4: {
                auto& matrix  = std::get<glm::mat4>(value);
                bool  changed = false;
                for (int column = 0; column < 4; ++column)
                {
                    ImGui::PushID(column);
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    changed |= ImGui::DragFloat4("##column", &matrix[column][0], info.speed);
                    ImGui::PopID();
                }
                return changed;
            }
            default:
                throw std::logic_error("Unsupported property Inspector kind");
        }
    }
} // namespace vultra
