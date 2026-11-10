#include "generated/vshader_lexer.h"
#include "generated/vshader_parser.h"
#include "rhi/shader_cache.hpp"

#include <vultra/assets/shader_asset.hpp>

#include <nlohmann/json.hpp>

#include <charconv>
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

        std::unexpected<std::string>
        fail(const std::filesystem::path& file, const Context* context, std::string message)
        {
            return std::unexpected(location(file, context->start).describe() + ": " + message);
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

        std::expected<std::string, std::string> unquote(const std::filesystem::path& file,
                                                        antlr4::tree::TerminalNode*  node)
        {
            auto decoded = nlohmann::json::parse(node->getText(), nullptr, false);
            if (!decoded.is_string())
            {
                return std::unexpected(location(file, node->getSymbol()).describe() + ": Invalid quoted string");
            }
            return decoded.get_ref<const std::string&>();
        }

        std::expected<double, std::string>
        number(const std::filesystem::path& file, Context* context, std::string_view text)
        {
            auto parsed = ShaderStateValue {std::string(text), false}.numberExpected();
            if (!parsed)
            {
                return fail(file, context, "Expected a representable finite number");
            }
            return *parsed;
        }

        std::expected<int32_t, std::string> integer(const std::filesystem::path& file, Context* context, double value)
        {
            if (std::trunc(value) != value || value < std::numeric_limits<int32_t>::min() ||
                value > std::numeric_limits<int32_t>::max())
            {
                return fail(file, context, "Expected a signed 32-bit integer");
            }
            return int32_t(value);
        }

        std::expected<ShaderProperty, std::string> readProperty(const std::filesystem::path& file,
                                                                Parser::PropertyContext*     context)
        {
            ShaderProperty property;
            property.name = context->IDENTIFIER()->getText();
            auto label    = unquote(file, context->STRING());
            if (!label)
            {
                return std::unexpected(label.error());
            }
            property.label    = std::move(*label);
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
                return fail(file, type, "Unknown property type: " + name);
            }
            property.type = types.at(name);
            if (property.type == ShaderPropertyType::eRange)
            {
                if (type->NUMBER().size() != 2)
                {
                    return fail(file, type, "Range requires a minimum and maximum");
                }
                auto minimum = number(file, type, type->NUMBER(0)->getText());
                auto maximum = number(file, type, type->NUMBER(1)->getText());
                if (!minimum || !maximum)
                {
                    return std::unexpected(minimum ? maximum.error() : minimum.error());
                }
                property.minimum = *minimum;
                property.maximum = *maximum;
                if (property.minimum >= property.maximum)
                {
                    return fail(file, type, "Range minimum must be less than maximum");
                }
            }
            else if (!type->NUMBER().empty())
            {
                return fail(file, type, "Only Range accepts numeric type arguments");
            }
            auto* value = context->value();
            if (property.isTexture())
            {
                if (!value->STRING())
                {
                    return fail(file, value, "Texture defaults must be quoted builtin names or an empty string");
                }
                ShaderTextureValue texture;
                auto               builtin = unquote(file, value->STRING());
                if (!builtin)
                {
                    return std::unexpected(builtin.error());
                }
                texture.builtin = std::move(*builtin);
                if (!texture.builtin.empty() && texture.builtin != "white" && texture.builtin != "black" &&
                    texture.builtin != "normal")
                {
                    return fail(file, value, "Unknown builtin texture: " + texture.builtin);
                }
                property.defaultValue = std::move(texture);
            }
            else if (property.type == ShaderPropertyType::eVector || property.type == ShaderPropertyType::eColor)
            {
                if (!value->LPAREN() || value->NUMBER().size() != 4)
                {
                    return fail(file, value, "Vector and Color defaults require four components");
                }
                std::array<float, 4> components;
                for (size_t index = 0; index < components.size(); ++index)
                {
                    auto component = number(file, value, value->NUMBER(index)->getText());
                    if (!component)
                    {
                        return std::unexpected(component.error());
                    }
                    components[index] = float(*component);
                }
                property.defaultValue = components;
            }
            else if (property.type == ShaderPropertyType::eBoolean)
            {
                if (!value->IDENTIFIER() || (value->getText() != "true" && value->getText() != "false"))
                {
                    return fail(file, value, "Boolean defaults must be true or false");
                }
                property.defaultValue = value->getText() == "true";
            }
            else
            {
                if (value->LPAREN() || value->NUMBER().size() != 1)
                {
                    return fail(file, value, "Expected a scalar numeric default");
                }
                const auto scalar = number(file, value, value->NUMBER(0)->getText());
                if (!scalar)
                {
                    return std::unexpected(scalar.error());
                }
                if (property.type == ShaderPropertyType::eInteger)
                {
                    auto parsed = integer(file, value, *scalar);
                    if (!parsed)
                    {
                        return std::unexpected(parsed.error());
                    }
                    property.defaultValue = *parsed;
                }
                else
                {
                    property.defaultValue = float(*scalar);
                }
            }
            std::set<std::string> attributes;
            for (auto* attribute : context->attribute())
            {
                const auto attributeName = attribute->IDENTIFIER()->getText();
                if (!attributes.insert(attributeName).second)
                {
                    return fail(file, attribute, "Duplicate property attribute: " + attributeName);
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
                        return fail(file, attribute, "Enum requires label/integer pairs");
                    }
                    std::set<std::string> labels;
                    std::set<int32_t>     values;
                    for (size_t index = 0; index < arguments.size(); index += 2)
                    {
                        if ((!arguments[index]->IDENTIFIER() && !arguments[index]->STRING()) ||
                            !arguments[index + 1]->NUMBER())
                        {
                            return fail(file, attribute, "Enum requires label/integer pairs");
                        }
                        auto label   = arguments[index]->STRING() ?
                                           unquote(file, arguments[index]->STRING()) :
                                           std::expected<std::string, std::string>(arguments[index]->getText());
                        auto numeric = number(file, attribute, arguments[index + 1]->getText());
                        if (!label || !numeric)
                        {
                            return std::unexpected(label ? numeric.error() : label.error());
                        }
                        auto enumValue = integer(file, attribute, *numeric);
                        if (!enumValue)
                        {
                            return std::unexpected(enumValue.error());
                        }
                        if (!labels.insert(*label).second || !values.insert(*enumValue).second)
                        {
                            return fail(file, attribute, "Duplicate Enum label or value");
                        }
                        property.enumeration.emplace_back(std::move(*label), *enumValue);
                    }
                }
                else
                {
                    return fail(file, attribute, "Unknown property attribute: " + attributeName);
                }
                if (attributeName != "Enum" && !attribute->attributeArgument().empty())
                {
                    return fail(file, attribute, "This property attribute does not take arguments");
                }
            }
            if (auto valid = property.validateExpected(property.defaultValue); !valid)
            {
                return std::unexpected(valid.error());
            }
            if ((attributes.contains("SRGB") && attributes.contains("Linear")) || (property.normal && property.srgb) ||
                ((attributes.contains("SRGB") || attributes.contains("Linear")) && !property.isTexture()))
            {
                return fail(file, context, "Texture usage attributes are conflicting or require a texture");
            }
            if ((property.hdr && property.type != ShaderPropertyType::eColor) ||
                ((property.normal || property.noScaleOffset) && !property.isTexture()) ||
                (!property.enumeration.empty() && property.type != ShaderPropertyType::eInteger))
            {
                return fail(file, context, "Property attribute does not match its type");
            }
            return property;
        }

        std::expected<ShaderState, std::string> readState(const std::filesystem::path&              file,
                                                          const std::vector<Parser::StateContext*>& contexts)
        {
            ShaderState state;
            state.location.file = file;
            for (auto* context : contexts)
            {
                state.location = location(file, context->start);
                auto add       = [&](const std::string&            name,
                               std::vector<ShaderStateValue> values) -> std::expected<void, std::string>
                {
                    if (!state.commands.emplace(name, std::move(values)).second)
                    {
                        return fail(file, context, "Duplicate render state: " + name);
                    }
                    return {};
                };
                if (context->STENCIL())
                {
                    if (auto added = add("Stencil", {{"On", false}}); !added)
                    {
                        return std::unexpected(added.error());
                    }
                    const auto names = context->IDENTIFIER();
                    for (size_t index = 0; index < names.size(); ++index)
                    {
                        auto* value = context->stateValue(index);
                        auto  added = add("Stencil." + names[index]->getText(),
                                          {{value->LBRACKET() ? value->IDENTIFIER()->getText() : value->getText(),
                                            value->LBRACKET() != nullptr}});
                        if (!added)
                        {
                            return std::unexpected(added.error());
                        }
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
                    if (auto added = add(context->start->getText(), std::move(values)); !added)
                    {
                        return std::unexpected(added.error());
                    }
                }
            }
            return state;
        }

        std::expected<std::map<std::string, std::string, std::less<>>, std::string>
        readTags(const std::filesystem::path& file, const std::vector<Parser::TagsContext*>& contexts)
        {
            std::map<std::string, std::string, std::less<>> tags;
            for (auto* context : contexts)
            {
                const auto values = context->STRING();
                for (size_t index = 0; index < values.size(); index += 2)
                {
                    auto key   = unquote(file, values[index]);
                    auto value = unquote(file, values[index + 1]);
                    if (!key || !value)
                    {
                        return std::unexpected(key ? value.error() : key.error());
                    }
                    if (!tags.emplace(*key, std::move(*value)).second)
                    {
                        return fail(file, context, "Duplicate shader tag: " + *key);
                    }
                }
            }
            return tags;
        }

        std::expected<uint64_t, std::string> readFeatures(const std::filesystem::path&                 file,
                                                          const std::vector<Parser::RequiresContext*>& contexts)
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
                        return fail(file, context, "Unknown required feature: " + feature);
                    }
                    if (result & bit)
                    {
                        return fail(file, context, "Duplicate required feature: " + feature);
                    }
                    result |= bit;
                }
            }
            return result;
        }

        template<class Program>
        std::expected<ShaderSourceBlock, std::string> readSource(const std::filesystem::path& file, Program* context)
        {
            const auto parts = context->PROGRAM_TEXT();
            if (parts.empty())
            {
                return fail(file, context, "Slang program cannot be empty");
            }
            ShaderSourceBlock block;
            block.location = location(file, parts.front()->getSymbol());
            for (auto* part : parts)
            {
                block.text += part->getText();
            }
            return block;
        }

        std::expected<ShaderVariant, std::string> readVariant(const std::filesystem::path& file,
                                                              Parser::VariantContext*      context)
        {
            ShaderVariant variant;
            auto          name = unquote(file, context->STRING());
            if (!name)
            {
                return std::unexpected(name.error());
            }
            variant.name     = std::move(*name);
            variant.location = location(file, context->start);
            if (context->constants().size() > 1 || context->modules().size() > 1 || context->defines().size() > 1 ||
                context->conformances().size() > 1)
            {
                return fail(file, context, "Duplicate variant section");
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
                        return fail(file, section, "Invalid or duplicate link-time constant: " + name);
                    }
                    if (type == "bool")
                    {
                        if (value != "true" && value != "false")
                        {
                            return fail(file, section, "Boolean constants require true or false");
                        }
                    }
                    else
                    {
                        const auto numeric = number(file, section, value);
                        if (!numeric)
                        {
                            return std::unexpected(numeric.error());
                        }
                        if (type == "int")
                        {
                            if (auto parsed = integer(file, section, *numeric); !parsed)
                            {
                                return std::unexpected(parsed.error());
                            }
                        }
                        if (type == "uint" &&
                            (std::trunc(*numeric) != *numeric || *numeric < 0 || *numeric > UINT32_MAX))
                        {
                            return fail(file, section, "Expected an unsigned 32-bit constant");
                        }
                    }
                    variant.constants += "export static const " + type + " " + name + " = " + value + ";\n";
                }
            }
            for (auto* section : context->modules())
            {
                for (auto* module : section->STRING())
                {
                    auto path = unquote(file, module);
                    if (!path)
                    {
                        return std::unexpected(path.error());
                    }
                    variant.modules.emplace_back(std::move(*path));
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
                        return fail(file, section, "Duplicate variant define: " + name);
                    }
                    auto decoded = value->getSymbol()->getType() == Parser::STRING ?
                                       unquote(file, value) :
                                       std::expected<std::string, std::string>(value->getText());
                    if (!decoded)
                    {
                        return std::unexpected(decoded.error());
                    }
                    variant.defines.push_back({name, std::move(*decoded)});
                }
            }
            for (auto* section : context->conformances())
            {
                for (auto* item : section->conformance())
                {
                    auto type          = unquote(file, item->STRING(0));
                    auto interfaceType = unquote(file, item->STRING(1));
                    if (!type || !interfaceType)
                    {
                        return std::unexpected(type ? interfaceType.error() : type.error());
                    }
                    const auto text   = item->NUMBER()->getText();
                    uint32_t   id     = 0;
                    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), id);
                    if (parsed.ec != std::errc {} || parsed.ptr != text.data() + text.size())
                    {
                        return fail(file, item, "Type conformance dispatch ID requires a uint32 decimal integer");
                    }
                    variant.typeConformances.push_back({std::move(*type), std::move(*interfaceType), id});
                }
            }
            return variant;
        }

        std::expected<ShaderPass, std::string> readPass(const std::filesystem::path& file, Parser::PassContext* context)
        {
            ShaderPass pass;
            pass.location = location(file, context->start);
            if (context->STRING().size() != 1 || context->program().size() != 1)
            {
                return fail(file, context, "Pass requires exactly one Name and SLANGPROGRAM");
            }
            auto name     = unquote(file, context->STRING(0));
            auto tags     = readTags(file, context->tags());
            auto state    = readState(file, context->state());
            auto features = readFeatures(file, context->requires_());
            auto source   = readSource(file, context->program(0));
            if (!name)
            {
                return std::unexpected(name.error());
            }
            if (!tags)
            {
                return std::unexpected(tags.error());
            }
            if (!state)
            {
                return std::unexpected(state.error());
            }
            if (!features)
            {
                return std::unexpected(features.error());
            }
            if (!source)
            {
                return std::unexpected(source.error());
            }
            pass.name = std::move(*name);
            if (const auto found = tags->find("LightMode"); found != tags->end())
            {
                pass.lightMode = found->second;
            }
            pass.state            = std::move(*state);
            pass.requiredFeatures = *features;
            pass.source           = std::move(*source);
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

    std::expected<ShaderAsset, std::string> ShaderAsset::parseExpected(const std::filesystem::path& file,
                                                                       std::string_view             text)
    {
        if (file.extension() != ".vshader" || text.size() > 16 * 1024 * 1024)
        {
            return std::unexpected("Expected a .vshader source no larger than 16 MiB");
        }
        auto absolute = detail::shaderAbsolutePathExpected(file);
        if (!absolute)
        {
            return std::unexpected(absolute.error());
        }
        const auto& source = *absolute;
        // ANTLR's strict UTF-8 input constructor requires valid source bytes. Preserve identity on rejection.
        const auto metadata = nlohmann::json::array({source.generic_string(), std::string(text)});
        const auto encoded  = metadata.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        if (nlohmann::json::parse(encoded, nullptr, false) != metadata)
        {
            return std::unexpected("Shader source and path require valid UTF-8");
        }
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
            return std::unexpected(errors.errors());
        }
        ShaderAsset asset;
        auto        name = unquote(source, root->STRING());
        if (!name)
        {
            return std::unexpected(name.error());
        }
        asset.name       = std::move(*name);
        asset.sourcePath = source;
        if (root->properties().size() > 1)
        {
            return fail(source, root, "Shader may contain only one Properties block");
        }
        for (auto* section : root->properties())
        {
            for (auto* property : section->property())
            {
                auto parsed = readProperty(source, property);
                if (!parsed)
                {
                    return std::unexpected(parsed.error());
                }
                asset.properties.push_back(std::move(*parsed));
            }
        }
        for (auto* variant : root->variant())
        {
            auto parsed = readVariant(source, variant);
            if (!parsed)
            {
                return std::unexpected(parsed.error());
            }
            asset.variants.push_back(std::move(*parsed));
        }
        if (asset.variants.empty())
        {
            asset.variants.push_back({"Default", {}, {}, {}, {}, {source, 1, 1}});
        }
        for (auto* context : root->subshader())
        {
            ShaderSubshader subshader;
            subshader.location = location(source, context->start);
            auto tags          = readTags(source, context->tags());
            auto state         = readState(source, context->state());
            auto features      = readFeatures(source, context->requires_());
            if (!tags)
            {
                return std::unexpected(tags.error());
            }
            if (!state)
            {
                return std::unexpected(state.error());
            }
            if (!features)
            {
                return std::unexpected(features.error());
            }
            subshader.tags             = std::move(*tags);
            subshader.state            = std::move(*state);
            subshader.requiredFeatures = *features;
            for (auto* common : context->common())
            {
                auto parsed = readSource(source, common);
                if (!parsed)
                {
                    return std::unexpected(parsed.error());
                }
                subshader.common.push_back(std::move(*parsed));
            }
            if (context->surface().size() > 1)
            {
                return fail(source, context, "SubShader may contain only one Surface");
            }
            if (!context->surface().empty())
            {
                auto* surface = context->surface(0);
                if (surface->MODEL().size() != 1 || surface->ENTRY().size() != 1 || surface->program().size() != 1)
                {
                    return fail(source, surface, "Surface requires one Model, Entry and SLANGPROGRAM");
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
                auto surfaceState  = readState(source, surface->state());
                auto surfaceSource = readSource(source, surface->program(0));
                if (!surfaceState)
                {
                    return std::unexpected(surfaceState.error());
                }
                if (!surfaceSource)
                {
                    return std::unexpected(surfaceSource.error());
                }
                result.state      = std::move(*surfaceState);
                result.source     = std::move(*surfaceSource);
                subshader.surface = std::move(result);
            }
            for (auto* pass : context->pass())
            {
                auto parsed = readPass(source, pass);
                if (!parsed)
                {
                    return std::unexpected(parsed.error());
                }
                subshader.passes.push_back(std::move(*parsed));
            }
            asset.subshaders.push_back(std::move(subshader));
        }
        if (auto valid = asset.validateExpected(); !valid)
        {
            return std::unexpected(valid.error());
        }
        return asset;
    }

    ShaderAsset ShaderAsset::parse(const std::filesystem::path& file, std::string_view text)
    {
        auto asset = parseExpected(file, text);
        if (!asset)
        {
            throw std::invalid_argument(asset.error());
        }
        return std::move(*asset);
    }
} // namespace vultra
