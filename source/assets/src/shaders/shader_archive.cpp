#include "rhi/shader_archive.hpp"
#include "shader_values.hpp"

#include <stdexcept>

namespace vultra
{
    namespace
    {
        using Json = nlohmann::json;

        Json encodeLocation(const ShaderSourceLocation& location)
        {
            return {{"file", location.file.generic_string()}, {"line", location.line}, {"column", location.column}};
        }

        ShaderSourceLocation decodeLocation(const Json& document)
        {
            const auto file   = document.at("file").get<std::string>();
            const auto line   = document.at("line").get<uint32_t>();
            const auto column = document.at("column").get<uint32_t>();
            if (file.size() > 4096 || file.find(char(0)) != std::string::npos || line == 0 || column == 0)
            {
                throw std::invalid_argument("Invalid shader source location");
            }
            return {std::filesystem::path(file), line, column};
        }

        Json encodeState(const ShaderState& state)
        {
            Json commands = Json::object();
            for (const auto& [name, values] : state.commands)
            {
                auto& encoded = commands[name] = Json::array();
                for (const auto& value : values)
                {
                    encoded.push_back({{"value", value.value}, {"property", value.property}});
                }
            }
            return {{"commands", commands}, {"location", encodeLocation(state.location)}};
        }

        ShaderState decodeState(const Json& document)
        {
            ShaderState state;
            state.location       = decodeLocation(document.at("location"));
            const auto& commands = document.at("commands");
            if (!commands.is_object() || commands.size() > 32)
            {
                throw std::invalid_argument("Invalid cooked shader state");
            }
            for (const auto& [name, values] : commands.items())
            {
                if (!values.is_array() || values.empty() || values.size() > 4 || name.size() > 128)
                {
                    throw std::invalid_argument("Invalid cooked shader state values");
                }
                auto& command = state.commands[name];
                for (const auto& value : values)
                {
                    const auto text = value.at("value").get<std::string>();
                    if (text.empty() || text.size() > 4096)
                    {
                        throw std::invalid_argument("Invalid shader state value");
                    }
                    command.push_back({text, value.at("property").get<bool>()});
                }
            }
            return state;
        }

        Json encodeProperty(const ShaderProperty& property)
        {
            return {{"name", property.name},
                    {"label", property.label},
                    {"type", property.type},
                    {"default", detail::encodePropertyValue(property.defaultValue)},
                    {"hdr", property.hdr},
                    {"normal", property.normal},
                    {"srgb", property.srgb},
                    {"hidden", property.hidden},
                    {"no_scale_offset", property.noScaleOffset},
                    {"minimum", property.minimum},
                    {"maximum", property.maximum},
                    {"enum", property.enumeration},
                    {"location", encodeLocation(property.location)}};
        }

        ShaderProperty decodeProperty(const Json& document)
        {
            ShaderProperty property;
            property.name   = document.at("name").get<std::string>();
            property.label  = document.at("label").get<std::string>();
            const auto type = document.at("type").get<uint32_t>();
            if (type > uint32_t(ShaderPropertyType::eTextureCubeArray) || property.name.size() > 4096 ||
                property.label.size() > 4096)
            {
                throw std::invalid_argument("Invalid cooked shader property type/name");
            }
            property.type          = ShaderPropertyType(type);
            property.defaultValue  = detail::decodePropertyValue(document.at("default"));
            property.hdr           = document.at("hdr").get<bool>();
            property.normal        = document.at("normal").get<bool>();
            property.srgb          = document.at("srgb").get<bool>();
            property.hidden        = document.at("hidden").get<bool>();
            property.noScaleOffset = document.at("no_scale_offset").get<bool>();
            property.minimum       = document.at("minimum").get<double>();
            property.maximum       = document.at("maximum").get<double>();
            property.enumeration   = document.at("enum").get<std::vector<std::pair<std::string, int32_t>>>();
            property.location      = decodeLocation(document.at("location"));
            return property;
        }
    } // namespace

    void ShaderAsset::save(const std::filesystem::path& file) const
    {
        validate();
        Json document {{"version", 1},
                       {"kind", "game_shader"},
                       {"name", name},
                       {"source", sourcePath.generic_string()},
                       {"compile_key", compileKey},
                       {"properties", Json::array()},
                       {"variants", Json::array()},
                       {"subshaders", Json::array()}};
        for (const auto& property : properties)
        {
            document["properties"].push_back(encodeProperty(property));
        }
        for (const auto& variant : variants)
        {
            document["variants"].push_back({{"name", variant.name}, {"location", encodeLocation(variant.location)}});
        }
        for (const auto& subshader : subshaders)
        {
            Json encoded {{"tags", subshader.tags},
                          {"state", encodeState(subshader.state)},
                          {"features", subshader.requiredFeatures},
                          {"location", encodeLocation(subshader.location)},
                          {"passes", Json::array()}};
            for (const auto& pass : subshader.passes)
            {
                if (pass.programs.size() != variants.size())
                {
                    throw std::invalid_argument("Cooked Pass must contain every selected explicit variant: " +
                                                pass.name);
                }
                Json entries = Json::array();
                for (const auto& entry : pass.entries)
                {
                    entries.push_back({{"name", entry.name}, {"stage", entry.stage}});
                }
                Json programs = Json::object();
                for (const auto& [variant, program] : pass.programs)
                {
                    programs[variant] = detail::encodeShaderProgram(program);
                }
                encoded["passes"].push_back({{"name", pass.name},
                                             {"light_mode", pass.lightMode},
                                             {"entries", entries},
                                             {"state", encodeState(pass.state)},
                                             {"features", pass.requiredFeatures},
                                             {"generated", pass.generated},
                                             {"location", encodeLocation(pass.location)},
                                             {"programs", programs}});
            }
            document["subshaders"].push_back(std::move(encoded));
        }
        detail::writeShaderArchive(file, document);
    }

    ShaderAsset ShaderAsset::load(const std::filesystem::path& file)
    {
        const auto document = detail::readShaderArchive(file);
        if (document.at("kind") != "game_shader")
        {
            throw std::invalid_argument("ShaderAsset requires a game shader artifact: " + file.string());
        }
        ShaderAsset asset;
        asset.name       = document.at("name").get<std::string>();
        asset.sourcePath = document.at("source").get<std::string>();
        asset.compileKey = document.at("compile_key").get<std::string>();
        if (!document.at("properties").is_array() || document.at("properties").size() > 4096 ||
            !document.at("variants").is_array() || document.at("variants").size() > 256 ||
            !document.at("subshaders").is_array() || document.at("subshaders").size() > 256 ||
            asset.sourcePath.generic_string().size() > 4096 || asset.compileKey.size() > 4096)
        {
            throw std::invalid_argument("Invalid game shader artifact sections");
        }
        for (const auto& property : document.at("properties"))
        {
            asset.properties.push_back(decodeProperty(property));
        }
        for (const auto& variant : document.at("variants"))
        {
            asset.variants.push_back(
                {variant.at("name").get<std::string>(), {}, {}, {}, decodeLocation(variant.at("location"))});
        }
        for (const auto& encoded : document.at("subshaders"))
        {
            ShaderSubshader subshader;
            subshader.tags             = encoded.at("tags").get<decltype(subshader.tags)>();
            subshader.state            = decodeState(encoded.at("state"));
            subshader.requiredFeatures = encoded.at("features").get<uint64_t>();
            subshader.location         = decodeLocation(encoded.at("location"));
            if (!encoded.at("passes").is_array() || encoded.at("passes").size() > 1024)
            {
                throw std::invalid_argument("Invalid cooked shader Pass count");
            }
            for (const auto& encodedPass : encoded.at("passes"))
            {
                ShaderPass pass;
                pass.name             = encodedPass.at("name").get<std::string>();
                pass.lightMode        = encodedPass.at("light_mode").get<std::string>();
                pass.state            = decodeState(encodedPass.at("state"));
                pass.requiredFeatures = encodedPass.at("features").get<uint64_t>();
                pass.generated        = encodedPass.at("generated").get<bool>();
                pass.location         = decodeLocation(encodedPass.at("location"));
                if (!encodedPass.at("entries").is_array() || encodedPass.at("entries").size() > 3 ||
                    !encodedPass.at("programs").is_object() ||
                    encodedPass.at("programs").size() != asset.variants.size())
                {
                    throw std::invalid_argument("Invalid cooked shader Pass entries/variants");
                }
                for (const auto& entry : encodedPass.at("entries"))
                {
                    const auto stage = entry.at("stage").get<uint64_t>();
                    if (stage > UINT32_MAX)
                    {
                        throw std::invalid_argument("Invalid cooked shader stage");
                    }
                    pass.entries.push_back({entry.at("name").get<std::string>(), VriShaderStageBits(stage)});
                }
                for (const auto& [name, program] : encodedPass.at("programs").items())
                {
                    pass.programs.emplace(name, detail::decodeShaderProgram(program));
                }
                subshader.passes.push_back(std::move(pass));
            }
            asset.subshaders.push_back(std::move(subshader));
        }
        asset.validate();
        return asset;
    }
} // namespace vultra
