#include <vultra/assets/shader_asset.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

namespace vultra
{
    std::expected<double, std::string> ShaderStateValue::numberExpected() const
    {
        if (property)
        {
            return std::unexpected("Render-state property requires a material instance: " + value);
        }
        char* end         = nullptr;
        errno             = 0;
        const auto number = std::strtod(value.c_str(), &end);
        if (errno == ERANGE || end == value.c_str() || end != value.c_str() + value.size() || !std::isfinite(number))
        {
            return std::unexpected("Expected a finite numeric render state: " + value);
        }
        return number;
    }

    const ShaderProperty& ShaderAsset::property(std::string_view name) const
    {
        auto found = propertyExpected(name);
        if (!found)
        {
            throw std::invalid_argument(found.error());
        }
        return **found;
    }

    std::expected<const ShaderProperty*, std::string> ShaderAsset::propertyExpected(std::string_view name) const
    {
        const auto found = std::ranges::find(properties, name, &ShaderProperty::name);
        if (found == properties.end())
        {
            return std::unexpected("Shader " + this->name + " has no property: " + std::string(name));
        }
        return &*found;
    }

    const ShaderVariant& ShaderAsset::variant(std::string_view name) const
    {
        auto found = variantExpected(name);
        if (!found)
        {
            throw std::invalid_argument(found.error());
        }
        return **found;
    }

    std::expected<const ShaderVariant*, std::string> ShaderAsset::variantExpected(std::string_view name) const
    {
        const auto found = std::ranges::find(variants, name, &ShaderVariant::name);
        if (found == variants.end())
        {
            return std::unexpected("Shader " + this->name + " has no variant: " + std::string(name));
        }
        return &*found;
    }

    const ShaderProgram& ShaderPass::program(std::string_view variant) const
    {
        auto found = programExpected(variant);
        if (!found)
        {
            throw std::invalid_argument(found.error());
        }
        return **found;
    }

    std::expected<const ShaderProgram*, std::string> ShaderPass::programExpected(std::string_view variant) const
    {
        const auto found = programs.find(variant);
        if (found == programs.end())
        {
            return std::unexpected("Pass " + name + " has no cooked variant: " + std::string(variant));
        }
        return &found->second;
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

        std::expected<void, std::string> validateState(const ShaderAsset& asset, const ShaderState& state)
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
                return std::unexpected(state.location.describe() + ": " + message);
            };
            for (const auto& [command, values] : state.commands)
            {
                if (values.empty())
                {
                    return fail("Empty state: " + command);
                }
                const bool numeric = command == "DepthBias" || command == "Stencil.Ref" ||
                                     command == "Stencil.ReadMask" || command == "Stencil.WriteMask";
                const bool mask    = command == "ColorMask";
                if (!numeric && !mask && !enums.contains(command))
                {
                    return fail("Unknown render state: " + command);
                }
                if (command == "BlendOp")
                {
                    if (values.size() != 1 && values.size() != 2)
                    {
                        return fail("BlendOp requires one color operation and optionally one alpha operation");
                    }
                }
                else if (command == "Blend")
                {
                    if (values.size() != 1 && values.size() != 2 && values.size() != 4)
                    {
                        return fail("Blend requires Off or two/four factors");
                    }
                    if (values.size() == 1 && (values[0].property || values[0].value != "Off"))
                    {
                        return fail("A single Blend value must be Off");
                    }
                }
                else if (command == "DepthBias")
                {
                    if (values.size() != 2 && values.size() != 3)
                    {
                        return fail("DepthBias requires constant and slope factors");
                    }
                }
                else if (command == "ColorMask")
                {
                    if (values.size() > 2)
                    {
                        return fail("ColorMask accepts a mask and optional attachment index");
                    }
                }
                else if (values.size() != 1)
                {
                    return fail("Render state requires one value: " + command);
                }
                for (size_t index = 0; index < values.size(); ++index)
                {
                    const auto& value = values[index];
                    if (value.property)
                    {
                        auto found = asset.propertyExpected(value.value);
                        if (!found)
                        {
                            return std::unexpected(found.error());
                        }
                        const auto& property = **found;
                        const bool  floating =
                            property.type == ShaderPropertyType::eFloat || property.type == ShaderPropertyType::eRange;
                        const bool boolean = command == "ZWrite" || command == "Stencil";
                        if ((boolean && property.type != ShaderPropertyType::eBoolean) ||
                            (!boolean && !numeric && property.type != ShaderPropertyType::eInteger) ||
                            (numeric && property.type != ShaderPropertyType::eInteger &&
                             !(command == "DepthBias" && floating)))
                        {
                            return fail("Render state property has the wrong type: " + value.value);
                        }
                    }
                    else if (numeric || (mask && (index == 1 || value.value == "0")))
                    {
                        auto parsed = value.numberExpected();
                        if (!parsed || std::abs(*parsed) > std::numeric_limits<float>::max())
                        {
                            return fail("Expected a numeric render state: " + command);
                        }
                        const auto number = *parsed;
                        if (command != "DepthBias" && (std::trunc(number) != number || number < 0 || number > 255))
                        {
                            return fail("Stencil values and attachment indices must be integers in [0, 255]");
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
                                return fail("ColorMask requires unique RGBA channels or zero");
                            }
                        }
                    }
                    else if (std::ranges::find(enums.at(command), value.value) == enums.at(command).end())
                    {
                        return fail("Invalid " + command + " value: " + value.value);
                    }
                }
            }
            return {};
        }
    } // namespace

    void ShaderAsset::validate() const
    {
        if (auto valid = validateExpected(); !valid)
        {
            throw std::invalid_argument(valid.error());
        }
    }

    std::expected<void, std::string> ShaderAsset::validateExpected() const
    {
        auto fail = [&](const ShaderSourceLocation& location, std::string message)
        {
            return std::unexpected(location.describe() + ": " + message);
        };
        if (name.empty() || name.size() > 4096 || subshaders.empty() || subshaders.size() > 256 ||
            properties.size() > 4096 || variants.empty() || variants.size() > 256)
        {
            return std::unexpected("Shader needs a name, SubShader and bounded property/variant counts");
        }
        std::set<std::string> propertyNames;
        for (const auto& property : properties)
        {
            if (!identifier(property.name) || property.name.starts_with("__vultra") ||
                !propertyNames.insert(property.name).second)
            {
                return fail(property.location, "Invalid or duplicate property: " + property.name);
            }
            if (auto valid = property.validateExpected(property.defaultValue); !valid)
            {
                return valid;
            }
            std::set<std::string> labels;
            std::set<int32_t>     enumValues;
            for (const auto& [label, value] : property.enumeration)
            {
                if (label.empty() || label.size() > 4096 || !labels.insert(label).second ||
                    !enumValues.insert(value).second)
                {
                    return fail(property.location, "Invalid or duplicate property enumeration item");
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
                return fail(property.location, "Property metadata does not match its type");
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
                        return fail(property.location,
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
                return fail(variant.location, "Invalid or duplicate variant: " + variant.name);
            }
            if (variant.typeConformances.size() > 4096 ||
                std::ranges::any_of(variant.typeConformances,
                                    [](const auto& conformance)
                                    {
                                        return conformance.typeName.size() > 4096 ||
                                               conformance.interfaceType.size() > 4096;
                                    }))
            {
                return fail(variant.location, "Shader variant type conformances exceed metadata limits");
            }
            if (auto valid = ShaderTypeConformance::validateExpected(variant.typeConformances); !valid)
            {
                return fail(variant.location, "Variant " + variant.name + ": " + valid.error());
            }
        }
        for (const auto& subshader : subshaders)
        {
            if (subshader.requiredFeatures & ~(VriFeature_MeshShader | VriFeature_RayQuery))
            {
                return fail(subshader.location, "Unsupported game shader capability bits");
            }
            if (auto valid = validateState(*this, subshader.state); !valid)
            {
                return valid;
            }
            if (!subshader.surface && subshader.passes.empty())
            {
                return fail(subshader.location, "SubShader requires a Surface or Pass");
            }
            if (const auto type = subshader.tags.find("RenderType");
                type != subshader.tags.end() && type->second != "Opaque" && type->second != "AlphaTest")
            {
                return fail(subshader.location, "Supported RenderType values are Opaque and AlphaTest");
            }
            if (subshader.surface)
            {
                if ((subshader.surface->model != "OpenPBR" && subshader.surface->model != "Unlit") ||
                    subshader.surface->entry.size() > 4096 || !identifier(subshader.surface->entry))
                {
                    return fail(subshader.surface->location, "Surface requires Model OpenPBR or Unlit and an Entry");
                }
                if (auto valid = validateState(*this, subshader.surface->state); !valid)
                {
                    return valid;
                }
            }
            std::set<std::string> passNames;
            std::set<std::string> lightModes;
            for (const auto& pass : subshader.passes)
            {
                if (pass.generated && !subshader.surface)
                {
                    return fail(pass.location, "Generated Pass requires Surface metadata");
                }
                if (pass.name.empty() || pass.name.size() > 256 || pass.name.find(char(0)) != std::string::npos ||
                    !passNames.insert(pass.name).second)
                {
                    return fail(pass.location, "Invalid or duplicate Pass name: " + pass.name);
                }
                constexpr std::array modes {"Forward",
                                            "DepthOnly",
                                            "ShadowCaster",
                                            "GBufferBase",
                                            "GBufferMaterial",
                                            "Custom"};
                if (std::ranges::find(modes, pass.lightMode) == modes.end())
                {
                    return fail(pass.location, "Unknown LightMode: " + pass.lightMode);
                }
                if (pass.lightMode != "Custom" && !lightModes.insert(pass.lightMode).second)
                {
                    return fail(pass.location, "Duplicate explicit LightMode: " + pass.lightMode);
                }
                if (pass.requiredFeatures & ~(VriFeature_MeshShader | VriFeature_RayQuery))
                {
                    return fail(pass.location, "Unsupported game Pass capability bits");
                }
                if (auto valid = validateState(*this, pass.state); !valid)
                {
                    return valid;
                }
                uint32_t stages = 0;
                for (const auto& entry : pass.entries)
                {
                    if (!identifier(entry.name) ||
                        (entry.stage != VriShaderStage_Vertex && entry.stage != VriShaderStage_Fragment &&
                         entry.stage != VriShaderStage_Task && entry.stage != VriShaderStage_Mesh &&
                         entry.stage != VriShaderStage_Compute))
                    {
                        return fail(pass.location, "Invalid game shader entry name or stage");
                    }
                    if (stages & uint32_t(entry.stage))
                    {
                        return fail(pass.location, "Duplicate shader stage");
                    }
                    stages |= uint32_t(entry.stage);
                }
                const auto graphics = uint32_t(VriShaderStage_Vertex | VriShaderStage_Fragment);
                const auto mesh     = uint32_t(VriShaderStage_Mesh | VriShaderStage_Fragment);
                if (stages != VriShaderStage_Compute && stages != graphics && stages != mesh &&
                    stages != (mesh | uint32_t(VriShaderStage_Task)) && stages != VriShaderStage_Vertex)
                {
                    return fail(pass.location, "Invalid shader stage combination");
                }
                if (stages == VriShaderStage_Vertex && pass.lightMode != "DepthOnly" &&
                    pass.lightMode != "ShadowCaster")
                {
                    return fail(pass.location, "Vertex-only passes require a depth/shadow purpose");
                }
                if (stages == VriShaderStage_Compute && (pass.lightMode != "Custom" || !pass.state.commands.empty()))
                {
                    return fail(pass.location, "Compute Pass requires Custom LightMode and no graphics state");
                }
                for (const auto& [variant, program] : pass.programs)
                {
                    if (auto found = variantExpected(variant); !found)
                    {
                        return std::unexpected(found.error());
                    }
                    if (auto descriptors = program.descriptorsExpected(pass.entries); !descriptors)
                    {
                        return std::unexpected(descriptors.error());
                    }
                }
            }
        }
        return {};
    }

    uint32_t ShaderAsset::selectSubshader(std::string_view                  pipeline,
                                          uint64_t                          features,
                                          std::span<const std::string_view> lightModes,
                                          std::string&                      diagnostics,
                                          const SubshaderCompatibility&     compatible) const
    {
        SubshaderCompatibilityExpected checked;
        if (compatible)
        {
            checked = [compatible](const ShaderSubshader& subshader) -> std::expected<std::string, std::string>
            {
                try
                {
                    return compatible(subshader);
                }
                catch (const std::exception& error)
                {
                    return std::unexpected(error.what());
                }
            };
        }
        auto selected = selectSubshaderExpected(pipeline, features, lightModes, diagnostics, checked);
        if (!selected)
        {
            throw std::invalid_argument(selected.error());
        }
        return *selected;
    }

    std::expected<uint32_t, std::string>
    ShaderAsset::selectSubshaderExpected(std::string_view                      pipeline,
                                         uint64_t                              features,
                                         std::span<const std::string_view>     lightModes,
                                         std::string&                          diagnostics,
                                         const SubshaderCompatibilityExpected& compatible) const
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
                auto result = compatible(subshader);
                if (!result)
                {
                    return std::unexpected(result.error());
                }
                reason = std::move(*result);
            }
            if (reason.empty())
            {
                diagnostics += "Selected SubShader " + std::to_string(index);
                return index;
            }
            diagnostics += "Rejected SubShader " + std::to_string(index) + ": " + reason + "\n";
        }
        return std::unexpected("Shader " + name + " has no applicable SubShader\n" + diagnostics);
    }
} // namespace vultra
