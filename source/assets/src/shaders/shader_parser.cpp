#include "generated/vshader_lexer.h"
#include "generated/vshader_parser.h"

#include <vultra/assets/shader_asset.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

namespace vultra
{
    namespace
    {
        using Parser  = vshader_parser;
        using Context = antlr4::ParserRuleContext;

        ShaderSourceLocation location(const std::filesystem::path& file, const antlr4::Token* token)
        {
            return {file, uint32_t(token->getLine()), uint32_t(token->getCharPositionInLine() + 1)};
        }

        [[noreturn]] void fail(const std::filesystem::path& file, const Context* context, std::string message)
        {
            throw std::invalid_argument(location(file, context->start).describe() + ": " + message);
        }

        class ErrorListener final : public antlr4::BaseErrorListener
        {
        public:
            explicit ErrorListener(std::filesystem::path file) :
                m_File(std::move(file))
            {
            }

            void syntaxError(antlr4::Recognizer*,
                             antlr4::Token*,
                             size_t             line,
                             size_t             column,
                             const std::string& message,
                             std::exception_ptr) override
            {
                m_Errors += ShaderSourceLocation {m_File, uint32_t(line), uint32_t(column + 1)}.describe() + ": " +
                            message + "\n";
            }

            const std::string& errors() const
            {
                return m_Errors;
            }

        private:
            std::filesystem::path m_File;
            std::string           m_Errors;
        };

        std::string unquote(antlr4::tree::TerminalNode* node)
        {
            return nlohmann::json::parse(node->getText()).get<std::string>();
        }

        double number(const std::filesystem::path& file, Context* context, std::string_view text)
        {
            size_t end    = 0;
            double result = 0;
            try
            {
                result = std::stod(std::string(text), &end);
            }
            catch (const std::exception&)
            {
                fail(file, context, "Expected a representable finite number");
            }
            if (!std::isfinite(result) || end != text.size())
            {
                fail(file, context, "Expected a finite number");
            }
            return result;
        }

        int32_t integer(const std::filesystem::path& file, Context* context, double value)
        {
            if (std::trunc(value) != value || value < std::numeric_limits<int32_t>::min() ||
                value > std::numeric_limits<int32_t>::max())
            {
                fail(file, context, "Expected a signed 32-bit integer");
            }
            return int32_t(value);
        }

        ShaderProperty readProperty(const std::filesystem::path& file, Parser::PropertyContext* context)
        {
            ShaderProperty property;
            property.name     = context->IDENTIFIER()->getText();
            property.label    = unquote(context->STRING());
            property.location = location(file, context->IDENTIFIER()->getSymbol());
            auto*      type   = context->propertyType();
            const auto name   = type->IDENTIFIER() ? type->IDENTIFIER()->getText() : type->TEXTURE_TYPE()->getText();
            const std::map<std::string, ShaderPropertyType> types {
                {"Float", ShaderPropertyType::eFloat},
                {"Range", ShaderPropertyType::eRange},
                {"Integer", ShaderPropertyType::eInteger},
                {"Boolean", ShaderPropertyType::eBoolean},
                {"Vector", ShaderPropertyType::eVector},
                {"Color", ShaderPropertyType::eColor},
                {"2D", ShaderPropertyType::eTexture2D},
                {"2DArray", ShaderPropertyType::eTexture2DArray},
                {"3D", ShaderPropertyType::eTexture3D},
                {"Cube", ShaderPropertyType::eTextureCube},
                {"CubeArray", ShaderPropertyType::eTextureCubeArray}};
            if (!types.contains(name))
            {
                fail(file, type, "Unknown property type: " + name);
            }
            property.type = types.at(name);
            if (property.type == ShaderPropertyType::eRange)
            {
                if (type->NUMBER().size() != 2)
                {
                    fail(file, type, "Range requires a minimum and maximum");
                }
                property.minimum = number(file, type, type->NUMBER(0)->getText());
                property.maximum = number(file, type, type->NUMBER(1)->getText());
                if (property.minimum >= property.maximum)
                {
                    fail(file, type, "Range minimum must be less than maximum");
                }
            }
            else if (!type->NUMBER().empty())
            {
                fail(file, type, "Only Range accepts numeric type arguments");
            }
            auto* value = context->value();
            if (property.isTexture())
            {
                if (!value->STRING())
                {
                    fail(file, value, "Texture defaults must be quoted builtin names or an empty string");
                }
                ShaderTextureValue texture;
                texture.builtin = unquote(value->STRING());
                if (!texture.builtin.empty() && texture.builtin != "white" && texture.builtin != "black" &&
                    texture.builtin != "normal")
                {
                    fail(file, value, "Unknown builtin texture: " + texture.builtin);
                }
                property.defaultValue = std::move(texture);
            }
            else if (property.type == ShaderPropertyType::eVector || property.type == ShaderPropertyType::eColor)
            {
                if (!value->LPAREN() || value->NUMBER().size() != 4)
                {
                    fail(file, value, "Vector and Color defaults require four components");
                }
                std::array<float, 4> components;
                for (size_t index = 0; index < components.size(); ++index)
                {
                    components[index] = float(number(file, value, value->NUMBER(index)->getText()));
                }
                property.defaultValue = components;
            }
            else if (property.type == ShaderPropertyType::eBoolean)
            {
                if (!value->IDENTIFIER() || (value->getText() != "true" && value->getText() != "false"))
                {
                    fail(file, value, "Boolean defaults must be true or false");
                }
                property.defaultValue = value->getText() == "true";
            }
            else
            {
                if (value->LPAREN() || value->NUMBER().size() != 1)
                {
                    fail(file, value, "Expected a scalar numeric default");
                }
                const auto scalar = number(file, value, value->NUMBER(0)->getText());
                if (property.type == ShaderPropertyType::eInteger)
                {
                    property.defaultValue = integer(file, value, scalar);
                }
                else
                {
                    property.defaultValue = float(scalar);
                }
            }
            std::set<std::string> attributes;
            for (auto* attribute : context->attribute())
            {
                const auto attributeName = attribute->IDENTIFIER()->getText();
                if (!attributes.insert(attributeName).second)
                {
                    fail(file, attribute, "Duplicate property attribute: " + attributeName);
                }
                if (attributeName == "HDR")
                {
                    property.hdr = true;
                }
                else if (attributeName == "Normal")
                {
                    property.normal = true;
                }
                else if (attributeName == "SRGB")
                {
                    property.srgb = true;
                }
                else if (attributeName == "Linear")
                {
                    property.srgb = false;
                }
                else if (attributeName == "HideInInspector")
                {
                    property.hidden = true;
                }
                else if (attributeName == "NoScaleOffset")
                {
                    property.noScaleOffset = true;
                }
                else if (attributeName == "Enum")
                {
                    const auto arguments = attribute->attributeArgument();
                    if (arguments.empty() || arguments.size() % 2 != 0)
                    {
                        fail(file, attribute, "Enum requires label/integer pairs");
                    }
                    std::set<std::string> labels;
                    std::set<int32_t>     values;
                    for (size_t index = 0; index < arguments.size(); index += 2)
                    {
                        if ((!arguments[index]->IDENTIFIER() && !arguments[index]->STRING()) ||
                            !arguments[index + 1]->NUMBER())
                        {
                            fail(file, attribute, "Enum requires label/integer pairs");
                        }
                        const auto label = arguments[index]->STRING() ? unquote(arguments[index]->STRING()) :
                                                                        arguments[index]->getText();
                        const auto enumValue =
                            integer(file, attribute, number(file, attribute, arguments[index + 1]->getText()));
                        if (!labels.insert(label).second || !values.insert(enumValue).second)
                        {
                            fail(file, attribute, "Duplicate Enum label or value");
                        }
                        property.enumeration.emplace_back(label, enumValue);
                    }
                }
                else
                {
                    fail(file, attribute, "Unknown property attribute: " + attributeName);
                }
                if (attributeName != "Enum" && !attribute->attributeArgument().empty())
                {
                    fail(file, attribute, "This property attribute does not take arguments");
                }
            }
            property.validate(property.defaultValue);
            if ((attributes.contains("SRGB") && attributes.contains("Linear")) || (property.normal && property.srgb) ||
                ((attributes.contains("SRGB") || attributes.contains("Linear")) && !property.isTexture()))
            {
                fail(file, context, "Texture usage attributes are conflicting or require a texture");
            }
            if ((property.hdr && property.type != ShaderPropertyType::eColor) ||
                ((property.normal || property.noScaleOffset) && !property.isTexture()) ||
                (!property.enumeration.empty() && property.type != ShaderPropertyType::eInteger))
            {
                fail(file, context, "Property attribute does not match its type");
            }
            return property;
        }

        ShaderState readState(const std::filesystem::path& file, const std::vector<Parser::StateContext*>& contexts)
        {
            ShaderState state;
            state.location.file = file;
            for (auto* context : contexts)
            {
                state.location = location(file, context->start);
                auto add       = [&](const std::string& name, std::vector<ShaderStateValue> values)
                {
                    if (!state.commands.emplace(name, std::move(values)).second)
                    {
                        fail(file, context, "Duplicate render state: " + name);
                    }
                };
                if (context->STENCIL())
                {
                    add("Stencil", {{"On", false}});
                    const auto names = context->IDENTIFIER();
                    for (size_t index = 0; index < names.size(); ++index)
                    {
                        auto* value = context->stateValue(index);
                        add("Stencil." + names[index]->getText(),
                            {{value->LBRACKET() ? value->IDENTIFIER()->getText() : value->getText(),
                              value->LBRACKET() != nullptr}});
                    }
                }
                else
                {
                    std::vector<ShaderStateValue> values;
                    for (auto* value : context->stateValue())
                    {
                        values.push_back({value->LBRACKET() ? value->IDENTIFIER()->getText() : value->getText(),
                                          value->LBRACKET() != nullptr});
                    }
                    if (context->NUMBER())
                    {
                        values.push_back({context->NUMBER()->getText(), false});
                    }
                    add(context->start->getText(), std::move(values));
                }
            }
            return state;
        }

        std::map<std::string, std::string, std::less<>> readTags(const std::filesystem::path&             file,
                                                                 const std::vector<Parser::TagsContext*>& contexts)
        {
            std::map<std::string, std::string, std::less<>> tags;
            for (auto* context : contexts)
            {
                const auto values = context->STRING();
                for (size_t index = 0; index < values.size(); index += 2)
                {
                    const auto key = unquote(values[index]);
                    if (!tags.emplace(key, unquote(values[index + 1])).second)
                    {
                        fail(file, context, "Duplicate shader tag: " + key);
                    }
                }
            }
            return tags;
        }

        uint64_t readFeatures(const std::filesystem::path& file, const std::vector<Parser::RequiresContext*>& contexts)
        {
            uint64_t result = 0;
            for (auto* context : contexts)
            {
                for (auto* name : context->IDENTIFIER())
                {
                    const auto feature = name->getText();
                    uint64_t   bit     = 0;
                    if (feature == "MeshShader")
                    {
                        bit = VriFeature_MeshShader;
                    }
                    else if (feature == "RayQuery")
                    {
                        bit = VriFeature_RayQuery;
                    }
                    else
                    {
                        fail(file, context, "Unknown required feature: " + feature);
                    }
                    if (result & bit)
                    {
                        fail(file, context, "Duplicate required feature: " + feature);
                    }
                    result |= bit;
                }
            }
            return result;
        }

        template<class Program>
        ShaderSourceBlock readSource(const std::filesystem::path& file, Program* context)
        {
            const auto parts = context->PROGRAM_TEXT();
            if (parts.empty())
            {
                fail(file, context, "Slang program cannot be empty");
            }
            ShaderSourceBlock block;
            block.location = location(file, parts.front()->getSymbol());
            for (auto* part : parts)
            {
                block.text += part->getText();
            }
            return block;
        }

        ShaderVariant readVariant(const std::filesystem::path& file, Parser::VariantContext* context)
        {
            ShaderVariant variant;
            variant.name     = unquote(context->STRING());
            variant.location = location(file, context->start);
            if (context->constants().size() > 1 || context->modules().size() > 1 || context->defines().size() > 1)
            {
                fail(file, context, "Duplicate variant section");
            }
            std::set<std::string> constants;
            for (auto* section : context->constants())
            {
                for (size_t index = 2; index + 3 < section->children.size() - 1; index += 4)
                {
                    const auto type  = section->children[index]->getText();
                    const auto name  = section->children[index + 1]->getText();
                    const auto value = section->children[index + 3]->getText();
                    if ((type != "bool" && type != "int" && type != "uint" && type != "float") ||
                        !constants.insert(name).second)
                    {
                        fail(file, section, "Invalid or duplicate link-time constant: " + name);
                    }
                    if (type == "bool")
                    {
                        if (value != "true" && value != "false")
                        {
                            fail(file, section, "Boolean constants require true or false");
                        }
                    }
                    else
                    {
                        const auto numeric = number(file, section, value);
                        if (type == "int")
                        {
                            integer(file, section, numeric);
                        }
                        if (type == "uint" && (std::trunc(numeric) != numeric || numeric < 0 || numeric > UINT32_MAX))
                        {
                            fail(file, section, "Expected an unsigned 32-bit constant");
                        }
                    }
                    variant.constants += "export static const " + type + " " + name + " = " + value + ";\n";
                }
            }
            for (auto* section : context->modules())
            {
                for (auto* module : section->STRING())
                {
                    variant.modules.emplace_back(unquote(module));
                }
            }
            std::set<std::string> defines;
            for (auto* section : context->defines())
            {
                for (size_t index = 2; index + 2 < section->children.size() - 1; index += 3)
                {
                    const auto name  = section->children[index]->getText();
                    auto*      value = dynamic_cast<antlr4::tree::TerminalNode*>(section->children[index + 2]);
                    if (!defines.insert(name).second)
                    {
                        fail(file, section, "Duplicate variant define: " + name);
                    }
                    variant.defines.push_back({name,
                                               value->getSymbol()->getType() == Parser::STRING ?
                                                   nlohmann::json::parse(value->getText()).get<std::string>() :
                                                   value->getText()});
                }
            }
            return variant;
        }

        ShaderPass readPass(const std::filesystem::path& file, Parser::PassContext* context)
        {
            ShaderPass pass;
            pass.location = location(file, context->start);
            if (context->STRING().size() != 1 || context->program().size() != 1)
            {
                fail(file, context, "Pass requires exactly one Name and SLANGPROGRAM");
            }
            pass.name       = unquote(context->STRING(0));
            const auto tags = readTags(file, context->tags());
            if (const auto found = tags.find("LightMode"); found != tags.end())
            {
                pass.lightMode = found->second;
            }
            pass.state            = readState(file, context->state());
            pass.requiredFeatures = readFeatures(file, context->requires_());
            pass.source           = readSource(file, context->program(0));
            for (auto* stage : context->stage())
            {
                const auto         type = stage->start->getType();
                VriShaderStageBits bit  = VriShaderStage_Compute;
                if (type == Parser::VERTEX)
                {
                    bit = VriShaderStage_Vertex;
                }
                else if (type == Parser::FRAGMENT)
                {
                    bit = VriShaderStage_Fragment;
                }
                else if (type == Parser::TASK)
                {
                    bit = VriShaderStage_Task;
                }
                else if (type == Parser::MESH)
                {
                    bit = VriShaderStage_Mesh;
                }
                if (bit == VriShaderStage_Mesh || bit == VriShaderStage_Task)
                {
                    pass.requiredFeatures |= VriFeature_MeshShader;
                }
                pass.entries.push_back({stage->IDENTIFIER()->getText(), bit});
            }
            return pass;
        }
    } // namespace

    ShaderAsset ShaderAsset::parse(const std::filesystem::path& file, std::string_view text)
    {
        if (file.extension() != ".vshader" || text.size() > 16 * 1024 * 1024)
        {
            throw std::invalid_argument("Expected a .vshader source no larger than 16 MiB");
        }
        const auto               source = std::filesystem::absolute(file).lexically_normal();
        antlr4::ANTLRInputStream input {std::string(text)};
        vshader_lexer            lexer(&input);
        ErrorListener            errors(source);
        lexer.removeErrorListeners();
        lexer.addErrorListener(&errors);
        antlr4::CommonTokenStream tokens(&lexer);
        Parser                    parser(&tokens);
        parser.removeErrorListeners();
        parser.addErrorListener(&errors);
        auto* root = parser.file();
        if (!errors.errors().empty())
        {
            throw std::invalid_argument(errors.errors());
        }
        ShaderAsset asset;
        asset.name       = unquote(root->STRING());
        asset.sourcePath = source;
        if (root->properties().size() > 1)
        {
            fail(source, root, "Shader may contain only one Properties block");
        }
        for (auto* section : root->properties())
        {
            for (auto* property : section->property())
            {
                asset.properties.push_back(readProperty(source, property));
            }
        }
        for (auto* variant : root->variant())
        {
            asset.variants.push_back(readVariant(source, variant));
        }
        if (asset.variants.empty())
        {
            asset.variants.push_back({"Default", {}, {}, {}, {source, 1, 1}});
        }
        for (auto* context : root->subshader())
        {
            ShaderSubshader subshader;
            subshader.location         = location(source, context->start);
            subshader.tags             = readTags(source, context->tags());
            subshader.state            = readState(source, context->state());
            subshader.requiredFeatures = readFeatures(source, context->requires_());
            for (auto* common : context->common())
            {
                subshader.common.push_back(readSource(source, common));
            }
            if (context->surface().size() > 1)
            {
                fail(source, context, "SubShader may contain only one Surface");
            }
            if (!context->surface().empty())
            {
                auto* surface = context->surface(0);
                if (surface->MODEL().size() != 1 || surface->ENTRY().size() != 1 || surface->program().size() != 1)
                {
                    fail(source, surface, "Surface requires one Model, Entry and SLANGPROGRAM");
                }
                ShaderSurface result;
                result.location = location(source, surface->start);
                for (size_t index = 0; index + 1 < surface->children.size(); ++index)
                {
                    if (surface->children[index]->getText() == "Model")
                    {
                        result.model = surface->children[index + 1]->getText();
                    }
                    if (surface->children[index]->getText() == "Entry")
                    {
                        result.entry = surface->children[index + 1]->getText();
                    }
                }
                result.state      = readState(source, surface->state());
                result.source     = readSource(source, surface->program(0));
                subshader.surface = std::move(result);
            }
            for (auto* pass : context->pass())
            {
                subshader.passes.push_back(readPass(source, pass));
            }
            asset.subshaders.push_back(std::move(subshader));
        }
        asset.validate();
        return asset;
    }
} // namespace vultra
