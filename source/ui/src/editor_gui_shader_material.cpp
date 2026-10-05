#include <vultra/ui/editor_gui.hpp>
#include <vultra/ui/editor_gui_shader_material.hpp>

#include <imgui.h>

#include <algorithm>
#include <format>

namespace vultra
{
    namespace
    {
        struct PropertyContext
        {
            const ShaderProperty&  property;
            const ProjectManifest* assets;
        };

        bool drawProperty(EditorGuiProperty, void* pointer, void* user)
        {
            auto&       value    = *static_cast<ShaderPropertyValue*>(pointer);
            const auto& context  = *static_cast<PropertyContext*>(user);
            const auto& property = context.property;
            bool        changed  = false;
            ImGui::SetNextItemWidth(-65);
            if (auto* number = std::get_if<float>(&value))
            {
                if (property.type == ShaderPropertyType::eRange)
                {
                    changed = ImGui::SliderFloat("##value", number, float(property.minimum), float(property.maximum));
                }
                else
                {
                    changed = ImGui::DragFloat("##value", number, 0.01f);
                }
            }
            else if (auto* integer = std::get_if<int32_t>(&value))
            {
                if (property.enumeration.empty())
                {
                    changed = ImGui::DragInt("##value", integer);
                }
                else
                {
                    auto       preview = std::to_string(*integer);
                    const auto found =
                        std::ranges::find(property.enumeration, *integer, &std::pair<std::string, int32_t>::second);
                    if (found != property.enumeration.end())
                    {
                        preview = found->first;
                    }
                    if (ImGui::BeginCombo("##value", preview.c_str()))
                    {
                        for (const auto& [label, number] : property.enumeration)
                        {
                            if (ImGui::Selectable(label.c_str(), *integer == number))
                            {
                                *integer = number;
                                changed  = true;
                            }
                        }
                        ImGui::EndCombo();
                    }
                }
            }
            else if (auto* boolean = std::get_if<bool>(&value))
            {
                changed = ImGui::Checkbox("##value", boolean);
            }
            else if (auto* vector = std::get_if<std::array<float, 4>>(&value))
            {
                if (property.type == ShaderPropertyType::eColor)
                {
                    const auto flags = property.hdr ? ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float : 0;
                    changed          = ImGui::ColorEdit4("##value", vector->data(), flags);
                }
                else
                {
                    changed = ImGui::DragFloat4("##value", vector->data(), 0.01f);
                }
            }
            else if (auto* texture = std::get_if<ShaderTextureValue>(&value))
            {
                auto preview = texture->asset.value.valid() ? texture->asset.value.toString() : texture->builtin;
                if (preview.empty())
                {
                    preview = "Required: unbound";
                }
                if (ImGui::BeginCombo("##texture", preview.c_str()))
                {
                    for (const auto* builtin : {"white", "black", "normal"})
                    {
                        if (ImGui::Selectable(builtin, texture->builtin == builtin && !texture->asset.value.valid()))
                        {
                            texture->asset   = {};
                            texture->builtin = builtin;
                            changed          = true;
                        }
                    }
                    if (context.assets)
                    {
                        for (const auto& asset : context.assets->assets())
                        {
                            const auto extension = asset.path.extension();
                            if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
                                extension == ".dds" || extension == ".hdr")
                            {
                                if (ImGui::Selectable(asset.path.generic_string().c_str(), texture->asset == asset.id))
                                {
                                    texture->asset = asset.id;
                                    texture->builtin.clear();
                                    changed = true;
                                }
                            }
                        }
                    }
                    ImGui::EndCombo();
                }
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Reset"))
            {
                value   = property.defaultValue;
                changed = true;
            }
            if (auto* texture = std::get_if<ShaderTextureValue>(&value);
                texture && !property.noScaleOffset &&
                (property.type == ShaderPropertyType::eTexture2D ||
                 property.type == ShaderPropertyType::eTexture2DArray))
            {
                ImGui::TextUnformatted("Scale / offset");
                ImGui::SetNextItemWidth(-1);
                changed |= ImGui::DragFloat4("##scale-offset", texture->scaleOffset.data(), 0.01f);
            }
            return changed;
        }
    } // namespace

    bool drawShaderMaterialInspector(EditorGui&             gui,
                                     const ShaderAsset&     shader,
                                     MaterialInstance&      material,
                                     const ProjectManifest* assets)
    {
        bool changed = false;
        ImGui::PushID(&material);
        if (ImGui::BeginCombo("Variant", material.variant.c_str()))
        {
            for (const auto& variant : shader.variants)
            {
                if (ImGui::Selectable(variant.name.c_str(), material.variant == variant.name))
                {
                    material.setVariant(shader, variant.name);
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        {
            EditorGuiInspector inspector(gui, "shader-properties");
            if (inspector)
            {
                for (const auto& property : shader.properties)
                {
                    if (property.hidden)
                    {
                        continue;
                    }
                    auto            value = material.value(shader, property.name);
                    PropertyContext context {property, assets};
                    if (inspector.property({property.name.c_str(), property.label.c_str()},
                                           &value,
                                           {drawProperty, &context}))
                    {
                        material.set(shader, property.name, std::move(value));
                        changed = true;
                    }
                }
            }
        }
        ImGui::PopID();
        return changed;
    }
} // namespace vultra
