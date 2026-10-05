#include <vultra/assets/shader_asset.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

namespace vultra
{
    std::string ShaderSourceLocation::describe() const
    {
        return file.generic_string() + ":" + std::to_string(line) + ":" + std::to_string(column);
    }

    bool ShaderProperty::isTexture() const
    {
        return type >= ShaderPropertyType::eTexture2D && type <= ShaderPropertyType::eTextureCubeArray;
    }

    void ShaderProperty::validate(const ShaderPropertyValue& value) const
    {
        auto fail = [&](std::string message)
        {
            throw std::invalid_argument(location.describe() + ": property " + name + ": " + message);
        };
        size_t expected = 0;
        if (type == ShaderPropertyType::eInteger)
        {
            expected = 1;
        }
        else if (type == ShaderPropertyType::eBoolean)
        {
            expected = 2;
        }
        else if (type == ShaderPropertyType::eVector || type == ShaderPropertyType::eColor)
        {
            expected = 3;
        }
        else if (isTexture())
        {
            expected = 4;
        }
        if (value.index() != expected)
        {
            fail("Value does not match the declared property type");
        }
        if (const auto* scalar = std::get_if<float>(&value); scalar && !std::isfinite(*scalar))
        {
            fail("Value must be finite");
        }
        if (const auto* vector = std::get_if<std::array<float, 4>>(&value))
        {
            for (const auto component : *vector)
            {
                if (!std::isfinite(component))
                {
                    fail("Components must be finite");
                }
            }
        }
        if (const auto* texture = std::get_if<ShaderTextureValue>(&value))
        {
            if ((!texture->builtin.empty() && texture->builtin != "white" && texture->builtin != "black" &&
                 texture->builtin != "normal") ||
                (texture->asset.value.valid() && !texture->builtin.empty()))
            {
                fail("Texture must reference either one asset or a known builtin");
            }
            for (const auto component : texture->scaleOffset)
            {
                if (!std::isfinite(component))
                {
                    fail("Texture scale/offset must be finite");
                }
            }
        }
    }

    const ShaderProperty& ShaderAsset::property(std::string_view name) const
    {
        for (const auto& property : properties)
        {
            if (property.name == name)
            {
                return property;
            }
        }
        throw std::invalid_argument("Shader " + this->name + " has no property: " + std::string(name));
    }

    const ShaderVariant& ShaderAsset::variant(std::string_view name) const
    {
        for (const auto& variant : variants)
        {
            if (variant.name == name)
            {
                return variant;
            }
        }
        throw std::invalid_argument("Shader " + this->name + " has no variant: " + std::string(name));
    }

    const ShaderProgram& ShaderPass::program(std::string_view variant) const
    {
        const auto found = programs.find(variant);
        if (found == programs.end())
        {
            throw std::invalid_argument("Pass " + name + " has no cooked variant: " + std::string(variant));
        }
        return found->second;
    }

    namespace
    {
        bool identifier(std::string_view name)
        {
            auto letter = [](char value)
            {
                return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || value == '_';
            };
            return !name.empty() && name.size() <= 256 && letter(name.front()) &&
                   std::ranges::all_of(name,
                                       [&](char value)
                                       {
                                           return letter(value) || (value >= '0' && value <= '9');
                                       });
        }

        void validateState(const ShaderAsset& asset, const ShaderState& state)
        {
            const std::map<std::string, std::vector<std::string>, std::less<>> enums {
                {"Cull", {"Off", "Front", "Back"}},
                {"FrontFace", {"CW", "CCW"}},
                {"ZWrite", {"On", "Off"}},
                {"ZTest", {"Never", "Less", "Equal", "LEqual", "Greater", "NotEqual", "GEqual", "Always"}},
                {"Blend",
                 {"Off",
                  "Zero",
                  "One",
                  "SrcColor",
                  "OneMinusSrcColor",
                  "DstColor",
                  "OneMinusDstColor",
                  "SrcAlpha",
                  "OneMinusSrcAlpha",
                  "DstAlpha",
                  "OneMinusDstAlpha",
                  "SrcAlphaSaturate",
                  "ConstantColor",
                  "OneMinusConstantColor",
                  "ConstantAlpha",
                  "OneMinusConstantAlpha"}},
                {"BlendOp", {"Add", "Subtract", "ReverseSubtract", "Min", "Max"}},
                {"Stencil", {"On", "Off"}},
                {"Stencil.Comp", {"Never", "Less", "Equal", "LEqual", "Greater", "NotEqual", "GEqual", "Always"}},
                {"Stencil.Pass", {"Keep", "Zero", "Replace", "IncrSat", "DecrSat", "Invert", "IncrWrap", "DecrWrap"}},
                {"Stencil.Fail", {"Keep", "Zero", "Replace", "IncrSat", "DecrSat", "Invert", "IncrWrap", "DecrWrap"}},
                {"Stencil.ZFail", {"Keep", "Zero", "Replace", "IncrSat", "DecrSat", "Invert", "IncrWrap", "DecrWrap"}}};
            auto fail = [&](std::string message)
            {
                throw std::invalid_argument(state.location.describe() + ": " + message);
            };
            for (const auto& [command, values] : state.commands)
            {
                if (values.empty())
                {
                    fail("Empty state: " + command);
                }
                const bool numeric = command == "DepthBias" || command == "Stencil.Ref" ||
                                     command == "Stencil.ReadMask" || command == "Stencil.WriteMask";
                const bool mask = command == "ColorMask";
                if (!numeric && !mask && !enums.contains(command))
                {
                    fail("Unknown render state: " + command);
                }
                if (command == "BlendOp")
                {
                    if (values.size() != 1 && values.size() != 2)
                    {
                        fail("BlendOp requires one color operation and optionally one alpha operation");
                    }
                }
                else if (command == "Blend")
                {
                    if (values.size() != 1 && values.size() != 2 && values.size() != 4)
                    {
                        fail("Blend requires Off or two/four factors");
                    }
                    if (values.size() == 1 && (values[0].property || values[0].value != "Off"))
                    {
                        fail("A single Blend value must be Off");
                    }
                }
                else if (command == "DepthBias")
                {
                    if (values.size() != 2 && values.size() != 3)
                    {
                        fail("DepthBias requires constant and slope factors");
                    }
                }
                else if (command == "ColorMask")
                {
                    if (values.size() > 2)
                    {
                        fail("ColorMask accepts a mask and optional attachment index");
                    }
                }
                else if (values.size() != 1)
                {
                    fail("Render state requires one value: " + command);
                }
                for (size_t index = 0; index < values.size(); ++index)
                {
                    const auto& value = values[index];
                    if (value.property)
                    {
                        const auto& property = asset.property(value.value);
                        const bool  floating =
                            property.type == ShaderPropertyType::eFloat || property.type == ShaderPropertyType::eRange;
                        const bool boolean = command == "ZWrite" || command == "Stencil";
                        if ((boolean && property.type != ShaderPropertyType::eBoolean) ||
                            (!boolean && !numeric && property.type != ShaderPropertyType::eInteger) ||
                            (numeric && property.type != ShaderPropertyType::eInteger &&
                             !(command == "DepthBias" && floating)))
                        {
                            fail("Render state property has the wrong type: " + value.value);
                        }
                    }
                    else if (numeric || (mask && (index == 1 || value.value == "0")))
                    {
                        size_t     end    = 0;
                        const auto number = std::stod(value.value, &end);
                        if (!std::isfinite(number) || std::abs(number) > std::numeric_limits<float>::max() ||
                            end != value.value.size())
                        {
                            fail("Expected a numeric render state: " + command);
                        }
                        if (command != "DepthBias" && (std::trunc(number) != number || number < 0 || number > 255))
                        {
                            fail("Stencil values and attachment indices must be integers in [0, 255]");
                        }
                    }
                    else if (mask)
                    {
                        std::set<char> channels;
                        for (const auto channel : value.value)
                        {
                            if (std::string_view("RGBA").find(channel) == std::string_view::npos ||
                                !channels.insert(channel).second)
                            {
                                fail("ColorMask requires unique RGBA channels or zero");
                            }
                        }
                    }
                    else if (std::ranges::find(enums.at(command), value.value) == enums.at(command).end())
                    {
                        fail("Invalid " + command + " value: " + value.value);
                    }
                }
            }
        }
    } // namespace

    void ShaderAsset::validate() const
    {
        auto fail = [&](const ShaderSourceLocation& location, std::string message)
        {
            throw std::invalid_argument(location.describe() + ": " + message);
        };
        if (name.empty() || name.size() > 4096 || subshaders.empty() || subshaders.size() > 256 ||
            properties.size() > 4096 || variants.empty() || variants.size() > 256)
        {
            throw std::invalid_argument("Shader needs a name, SubShader and bounded property/variant counts");
        }
        std::set<std::string> propertyNames;
        for (const auto& property : properties)
        {
            if (!identifier(property.name) || property.name.starts_with("__vultra") ||
                !propertyNames.insert(property.name).second)
            {
                fail(property.location, "Invalid or duplicate property: " + property.name);
            }
            property.validate(property.defaultValue);
            std::set<std::string> labels;
            std::set<int32_t>     enumValues;
            for (const auto& [label, value] : property.enumeration)
            {
                if (label.empty() || label.size() > 4096 || !labels.insert(label).second ||
                    !enumValues.insert(value).second)
                {
                    fail(property.location, "Invalid or duplicate property enumeration item");
                }
            }
            if ((property.hdr && property.type != ShaderPropertyType::eColor) ||
                ((property.normal || property.srgb || property.noScaleOffset) && !property.isTexture()) ||
                (property.normal && property.srgb) ||
                (!property.enumeration.empty() && property.type != ShaderPropertyType::eInteger) ||
                !std::isfinite(property.minimum) || !std::isfinite(property.maximum) ||
                std::abs(property.minimum) > std::numeric_limits<float>::max() ||
                std::abs(property.maximum) > std::numeric_limits<float>::max() ||
                (property.type == ShaderPropertyType::eRange && property.minimum >= property.maximum))
            {
                fail(property.location, "Property metadata does not match its type");
            }
        }
        for (const auto& property : properties)
        {
            if (property.isTexture())
            {
                for (const auto suffix : {"Sampler", "ScaleOffset", "Encoding"})
                {
                    if (propertyNames.contains(property.name + suffix))
                    {
                        fail(property.location,
                             "Property conflicts with a generated texture field: " + property.name + suffix);
                    }
                }
            }
        }
        std::set<std::string> variantNames;
        for (const auto& variant : variants)
        {
            if (variant.name.empty() || variant.name.size() > 256 || variant.name.find(char(0)) != std::string::npos ||
                !variantNames.insert(variant.name).second)
            {
                fail(variant.location, "Invalid or duplicate variant: " + variant.name);
            }
        }
        for (const auto& subshader : subshaders)
        {
            if (subshader.requiredFeatures & ~(VriFeature_MeshShader | VriFeature_RayQuery))
            {
                fail(subshader.location, "Unsupported game shader capability bits");
            }
            validateState(*this, subshader.state);
            if (!subshader.surface && subshader.passes.empty())
            {
                fail(subshader.location, "SubShader requires a Surface or Pass");
            }
            if (const auto type = subshader.tags.find("RenderType");
                type != subshader.tags.end() && type->second != "Opaque" && type->second != "AlphaTest")
            {
                fail(subshader.location, "Supported RenderType values are Opaque and AlphaTest");
            }
            if (subshader.surface)
            {
                if (subshader.surface->model != "OpenPBR" || subshader.surface->entry.empty())
                {
                    fail(subshader.surface->location, "Surface requires Model OpenPBR and an Entry");
                }
                validateState(*this, subshader.surface->state);
            }
            std::set<std::string> passNames;
            std::set<std::string> lightModes;
            for (const auto& pass : subshader.passes)
            {
                if (pass.name.empty() || pass.name.size() > 256 || pass.name.find(char(0)) != std::string::npos ||
                    !passNames.insert(pass.name).second)
                {
                    fail(pass.location, "Invalid or duplicate Pass name: " + pass.name);
                }
                constexpr std::array modes {"Forward",
                                            "DepthOnly",
                                            "ShadowCaster",
                                            "GBufferBase",
                                            "GBufferMaterial",
                                            "Custom"};
                if (std::ranges::find(modes, pass.lightMode) == modes.end())
                {
                    fail(pass.location, "Unknown LightMode: " + pass.lightMode);
                }
                if (pass.lightMode != "Custom" && !lightModes.insert(pass.lightMode).second)
                {
                    fail(pass.location, "Duplicate explicit LightMode: " + pass.lightMode);
                }
                if (pass.requiredFeatures & ~(VriFeature_MeshShader | VriFeature_RayQuery))
                {
                    fail(pass.location, "Unsupported game Pass capability bits");
                }
                validateState(*this, pass.state);
                uint32_t stages = 0;
                for (const auto& entry : pass.entries)
                {
                    if (!identifier(entry.name) ||
                        (entry.stage != VriShaderStage_Vertex && entry.stage != VriShaderStage_Fragment &&
                         entry.stage != VriShaderStage_Task && entry.stage != VriShaderStage_Mesh &&
                         entry.stage != VriShaderStage_Compute))
                    {
                        fail(pass.location, "Invalid game shader entry name or stage");
                    }
                    if (stages & uint32_t(entry.stage))
                    {
                        fail(pass.location, "Duplicate shader stage");
                    }
                    stages |= uint32_t(entry.stage);
                }
                const auto graphics = uint32_t(VriShaderStage_Vertex | VriShaderStage_Fragment);
                const auto mesh     = uint32_t(VriShaderStage_Mesh | VriShaderStage_Fragment);
                if (stages != VriShaderStage_Compute && stages != graphics && stages != mesh &&
                    stages != (mesh | uint32_t(VriShaderStage_Task)) && stages != VriShaderStage_Vertex)
                {
                    fail(pass.location, "Invalid shader stage combination");
                }
                if (stages == VriShaderStage_Vertex && pass.lightMode != "DepthOnly" &&
                    pass.lightMode != "ShadowCaster")
                {
                    fail(pass.location, "Vertex-only passes require a depth/shadow purpose");
                }
                if (stages == VriShaderStage_Compute && (pass.lightMode != "Custom" || !pass.state.commands.empty()))
                {
                    fail(pass.location, "Compute Pass requires Custom LightMode and no graphics state");
                }
                for (const auto& [variant, program] : pass.programs)
                {
                    this->variant(variant);
                    program.descriptors(pass.entries);
                }
            }
        }
    }

    uint32_t ShaderAsset::selectSubshader(std::string_view                  pipeline,
                                          uint64_t                          features,
                                          std::span<const std::string_view> lightModes,
                                          std::string&                      diagnostics,
                                          const SubshaderCompatibility&     compatible) const
    {
        diagnostics.clear();
        for (uint32_t index = 0; index < subshaders.size(); ++index)
        {
            const auto& subshader = subshaders[index];
            const auto  tag       = subshader.tags.find("RenderPipeline");
            std::string reason;
            if (tag != subshader.tags.end() && tag->second != pipeline)
            {
                reason = "render pipeline differs";
            }
            else if ((subshader.requiredFeatures & features) != subshader.requiredFeatures)
            {
                reason = "required device features are unavailable";
            }
            else
            {
                for (const auto mode : lightModes)
                {
                    const auto found =
                        std::ranges::find_if(subshader.passes,
                                             [&](const ShaderPass& pass)
                                             {
                                                 return pass.lightMode == mode &&
                                                        (pass.requiredFeatures & features) == pass.requiredFeatures;
                                             });
                    if (found == subshader.passes.end())
                    {
                        reason = "missing supported Pass for " + std::string(mode);
                        break;
                    }
                }
            }
            if (reason.empty() && compatible)
            {
                reason = compatible(subshader);
            }
            if (reason.empty())
            {
                diagnostics += "Selected SubShader " + std::to_string(index);
                return index;
            }
            diagnostics += "Rejected SubShader " + std::to_string(index) + ": " + reason + "\n";
        }
        throw std::invalid_argument("Shader " + name + " has no applicable SubShader\n" + diagnostics);
    }
} // namespace vultra
