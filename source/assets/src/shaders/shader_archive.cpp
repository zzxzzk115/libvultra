#include "rhi/shader_archive.hpp"
#include "shader_values.hpp"

#include <vultra/assets/asset_source.hpp>
#include <vultra/assets/shader_asset.hpp>

#include <cmath>
#include <stdexcept>

namespace vultra
{
    namespace
    {
        using Json = nlohmann::json;

        const Json* member(const Json& document, const char* name)
        {
            if (!document.is_object())
            {
                return nullptr;
            }
            const auto found = document.find(name);
            return found == document.end() ? nullptr : &*found;
        }

        bool text(const Json& document, const char* name, std::string& result)
        {
            const auto* value = member(document, name);
            if (!value || !value->is_string())
            {
                return false;
            }
            result = value->get_ref<const std::string&>();
            return result.size() <= 4096;
        }

        bool number(const Json& document, const char* name, uint64_t& result)
        {
            const auto* value = member(document, name);
            if (!value || !value->is_number_unsigned())
            {
                return false;
            }
            result = value->get<uint64_t>();
            return true;
        }

        bool boolean(const Json& document, const char* name, bool& result)
        {
            const auto* value = member(document, name);
            if (!value || !value->is_boolean())
            {
                return false;
            }
            result = value->get<bool>();
            return true;
        }

        std::expected<ShaderSourceLocation, std::string> decodeLocation(const Json& document)
        {
            std::string file;
            uint64_t    line;
            uint64_t    column;
            if (!text(document, "file", file) || file.find(char(0)) != std::string::npos ||
                !number(document, "line", line) || line == 0 || line > UINT32_MAX ||
                !number(document, "column", column) || column == 0 || column > UINT32_MAX)
            {
                return std::unexpected("Invalid shader source location");
            }
            return ShaderSourceLocation {std::filesystem::path(file), uint32_t(line), uint32_t(column)};
        }

        std::expected<ShaderState, std::string> decodeState(const Json& document)
        {
            const auto* location = member(document, "location");
            const auto* commands = member(document, "commands");
            if (!location || !commands || !commands->is_object() || commands->size() > 32)
            {
                return std::unexpected("Invalid cooked shader state");
            }
            auto decodedLocation = decodeLocation(*location);
            if (!decodedLocation)
            {
                return std::unexpected(decodedLocation.error());
            }
            ShaderState state;
            state.location = std::move(*decodedLocation);
            for (const auto& [name, values] : commands->items())
            {
                if (!values.is_array() || values.empty() || values.size() > 4 || name.size() > 128)
                {
                    return std::unexpected("Invalid cooked shader state values");
                }
                auto& command = state.commands[name];
                for (const auto& value : values)
                {
                    ShaderStateValue result;
                    if (!text(value, "value", result.value) || result.value.empty() ||
                        !boolean(value, "property", result.property))
                    {
                        return std::unexpected("Invalid shader state value");
                    }
                    command.push_back(std::move(result));
                }
            }
            return state;
        }

        std::expected<ShaderProperty, std::string> decodeProperty(const Json& document)
        {
            ShaderProperty property;
            uint64_t       type;
            const auto*    defaultValue = member(document, "default");
            const auto*    minimum      = member(document, "minimum");
            const auto*    maximum      = member(document, "maximum");
            const auto*    enumeration  = member(document, "enum");
            const auto*    location     = member(document, "location");
            if (!text(document, "name", property.name) || !text(document, "label", property.label) ||
                !number(document, "type", type) || type > uint64_t(ShaderPropertyType::eTextureCubeArray) ||
                !boolean(document, "hdr", property.hdr) || !boolean(document, "normal", property.normal) ||
                !boolean(document, "srgb", property.srgb) || !boolean(document, "hidden", property.hidden) ||
                !boolean(document, "no_scale_offset", property.noScaleOffset) || !defaultValue || !minimum ||
                !minimum->is_number() || !maximum || !maximum->is_number() || !enumeration ||
                !enumeration->is_array() || !location)
            {
                return std::unexpected("Invalid cooked shader property fields");
            }
            property.type        = ShaderPropertyType(type);
            property.minimum     = minimum->get<double>();
            property.maximum     = maximum->get<double>();
            auto value           = detail::decodePropertyValueExpected(*defaultValue);
            auto decodedLocation = decodeLocation(*location);
            if (!value || !decodedLocation)
            {
                return std::unexpected(value ? decodedLocation.error() : value.error());
            }
            property.defaultValue = std::move(*value);
            property.location     = std::move(*decodedLocation);
            for (const auto& item : *enumeration)
            {
                if (!item.is_array() || item.size() != 2 || !item[0].is_string() || !item[1].is_number_integer() ||
                    item[1].get<double>() < INT32_MIN || item[1].get<double>() > INT32_MAX)
                {
                    return std::unexpected("Invalid cooked shader property enumeration");
                }
                property.enumeration.emplace_back(item[0].get_ref<const std::string&>(), item[1].get<int32_t>());
            }
            return property;
        }

        std::expected<ShaderAsset, std::string> decodeAsset(const Json& document)
        {
            const auto* kind = member(document, "kind");
            if (!kind || *kind != "game_shader")
            {
                return std::unexpected("ShaderAsset requires a game shader artifact");
            }
            ShaderAsset asset;
            std::string source;
            const auto* properties = member(document, "properties");
            const auto* variants   = member(document, "variants");
            const auto* subshaders = member(document, "subshaders");
            if (!text(document, "name", asset.name) || !text(document, "source", source) ||
                source.find(char(0)) != std::string::npos || !text(document, "compile_key", asset.compileKey) ||
                !properties || !properties->is_array() || properties->size() > 4096 || !variants ||
                !variants->is_array() || variants->size() > 256 || !subshaders || !subshaders->is_array() ||
                subshaders->size() > 256)
            {
                return std::unexpected("Invalid game shader artifact sections");
            }
            asset.sourcePath = source;
            for (const auto& property : *properties)
            {
                auto decoded = decodeProperty(property);
                if (!decoded)
                {
                    return std::unexpected(decoded.error());
                }
                asset.properties.push_back(std::move(*decoded));
            }
            for (const auto& variant : *variants)
            {
                ShaderVariant result;
                const auto*   location = member(variant, "location");
                const auto*   conformances = member(variant, "type_conformances");
                if (!text(variant, "name", result.name) || !location || !conformances || !conformances->is_array() ||
                    conformances->size() > 4096)
                {
                    return std::unexpected("Invalid cooked shader variant");
                }
                auto decoded = decodeLocation(*location);
                if (!decoded)
                {
                    return std::unexpected(decoded.error());
                }
                result.location = std::move(*decoded);
                for (const auto& value : *conformances)
                {
                    ShaderTypeConformance conformance;
                    uint64_t              id;
                    if (!text(value, "type", conformance.typeName) ||
                        !text(value, "interface", conformance.interfaceType) || !number(value, "id", id) ||
                        id > UINT32_MAX)
                    {
                        return std::unexpected("Invalid cooked shader type conformance in variant: " + result.name);
                    }
                    conformance.id = uint32_t(id);
                    result.typeConformances.push_back(std::move(conformance));
                }
                asset.variants.push_back(std::move(result));
            }
            for (const auto& encoded : *subshaders)
            {
                ShaderSubshader subshader;
                const auto*     tags     = member(encoded, "tags");
                const auto*     state    = member(encoded, "state");
                const auto*     location = member(encoded, "location");
                const auto*     passes   = member(encoded, "passes");
                const auto*     surface  = member(encoded, "surface");
                if (!tags || !tags->is_object() || !state || !location || !surface || !passes || !passes->is_array() ||
                    passes->size() > 1024 || !number(encoded, "features", subshader.requiredFeatures))
                {
                    return std::unexpected("Invalid cooked SubShader fields");
                }
                auto decodedState    = decodeState(*state);
                auto decodedLocation = decodeLocation(*location);
                if (!decodedState || !decodedLocation)
                {
                    return std::unexpected(decodedState ? decodedLocation.error() : decodedState.error());
                }
                subshader.state    = std::move(*decodedState);
                subshader.location = std::move(*decodedLocation);
                if (!surface->is_null())
                {
                    ShaderSurface definition;
                    const auto*   surfaceState    = member(*surface, "state");
                    const auto*   surfaceLocation = member(*surface, "location");
                    if (!text(*surface, "model", definition.model) || !text(*surface, "entry", definition.entry) ||
                        !surfaceState || !surfaceLocation)
                    {
                        return std::unexpected("Invalid cooked Surface metadata");
                    }
                    auto decodedSurfaceState    = decodeState(*surfaceState);
                    auto decodedSurfaceLocation = decodeLocation(*surfaceLocation);
                    if (!decodedSurfaceState || !decodedSurfaceLocation)
                    {
                        return std::unexpected(decodedSurfaceState ? decodedSurfaceLocation.error() :
                                                                     decodedSurfaceState.error());
                    }
                    definition.state    = std::move(*decodedSurfaceState);
                    definition.location = std::move(*decodedSurfaceLocation);
                    subshader.surface   = std::move(definition);
                }
                for (const auto& [name, value] : tags->items())
                {
                    if (!value.is_string())
                    {
                        return std::unexpected("Invalid cooked SubShader tag");
                    }
                    subshader.tags.emplace(name, value.get_ref<const std::string&>());
                }
                for (const auto& encodedPass : *passes)
                {
                    ShaderPass  pass;
                    const auto* passState    = member(encodedPass, "state");
                    const auto* passLocation = member(encodedPass, "location");
                    const auto* entries      = member(encodedPass, "entries");
                    const auto* programs     = member(encodedPass, "programs");
                    if (!text(encodedPass, "name", pass.name) || !text(encodedPass, "light_mode", pass.lightMode) ||
                        !number(encodedPass, "features", pass.requiredFeatures) ||
                        !boolean(encodedPass, "generated", pass.generated) || !passState || !passLocation || !entries ||
                        !entries->is_array() || entries->size() > 3 || !programs || !programs->is_object() ||
                        programs->size() != asset.variants.size())
                    {
                        return std::unexpected("Invalid cooked shader Pass entries/variants");
                    }
                    auto decodedPassState    = decodeState(*passState);
                    auto decodedPassLocation = decodeLocation(*passLocation);
                    if (!decodedPassState || !decodedPassLocation)
                    {
                        return std::unexpected(decodedPassState ? decodedPassLocation.error() :
                                                                  decodedPassState.error());
                    }
                    pass.state    = std::move(*decodedPassState);
                    pass.location = std::move(*decodedPassLocation);
                    for (const auto& entry : *entries)
                    {
                        ShaderEntry result;
                        uint64_t    stage;
                        if (!text(entry, "name", result.name) || !number(entry, "stage", stage) || stage > UINT32_MAX)
                        {
                            return std::unexpected("Invalid cooked shader stage");
                        }
                        result.stage = VriShaderStageBits(stage);
                        pass.entries.push_back(std::move(result));
                    }
                    for (const auto& [name, program] : programs->items())
                    {
                        auto decoded = detail::decodeShaderProgramExpected(program);
                        if (!decoded)
                        {
                            return std::unexpected("Pass " + pass.name + ", variant " + name + ": " + decoded.error());
                        }
                        pass.programs.emplace(name, std::move(*decoded));
                    }
                    subshader.passes.push_back(std::move(pass));
                }
                asset.subshaders.push_back(std::move(subshader));
            }
            if (auto valid = asset.validateExpected(); !valid)
            {
                return std::unexpected(valid.error());
            }
            return asset;
        }
        Json encodeLocation(const ShaderSourceLocation& location)
        {
            return {{"file", location.file.generic_string()}, {"line", location.line}, {"column", location.column}};
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

    } // namespace

    std::expected<void, std::string> ShaderAsset::saveExpected(const std::filesystem::path& file) const
    {
        if (auto valid = validateExpected(); !valid)
        {
            return valid;
        }
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
            Json conformances = Json::array();
            for (const auto& conformance : variant.typeConformances)
            {
                conformances.push_back(
                    {{"type", conformance.typeName}, {"interface", conformance.interfaceType}, {"id", conformance.id}});
            }
            document["variants"].push_back({{"name", variant.name},
                                            {"location", encodeLocation(variant.location)},
                                            {"type_conformances", std::move(conformances)}});
        }
        for (const auto& subshader : subshaders)
        {
            Json surface;
            if (subshader.surface)
            {
                const auto& definition = *subshader.surface;
                surface                = {{"model", definition.model},
                                          {"entry", definition.entry},
                                          {"state", encodeState(definition.state)},
                                          {"location", encodeLocation(definition.location)}};
            }
            Json encoded {{"tags", subshader.tags},
                          {"state", encodeState(subshader.state)},
                          {"features", subshader.requiredFeatures},
                          {"location", encodeLocation(subshader.location)},
                          {"surface", std::move(surface)},
                          {"passes", Json::array()}};
            for (const auto& pass : subshader.passes)
            {
                if (pass.programs.size() != variants.size())
                {
                    return std::unexpected("Cooked Pass must contain every selected explicit variant: " + pass.name);
                }
                Json entries = Json::array();
                for (const auto& entry : pass.entries)
                {
                    entries.push_back({{"name", entry.name}, {"stage", entry.stage}});
                }
                Json programs = Json::object();
                for (const auto& [variant, program] : pass.programs)
                {
                    if (auto valid = detail::validateShaderProgramExpected(program); !valid)
                    {
                        return std::unexpected("Pass " + pass.name + ", variant " + variant + ": " + valid.error());
                    }
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
        return detail::writeShaderArchiveExpected(file, document);
    }

    std::expected<ShaderAsset, std::string> ShaderAsset::loadExpected(const std::filesystem::path& file,
                                                                      const AssetSource*           source)
    {
        if (source)
        {
            auto bytes = source->readExpected(file);
            if (!bytes)
            {
                return std::unexpected(bytes.error());
            }
            return loadExpected(*bytes, file);
        }
        auto document = detail::readShaderArchiveExpected(file);
        if (!document)
        {
            return std::unexpected(document.error());
        }
        auto asset = decodeAsset(*document);
        if (!asset)
        {
            return std::unexpected(asset.error() + ": " + file.string());
        }
        return asset;
    }

    std::expected<ShaderAsset, std::string> ShaderAsset::loadExpected(std::span<const std::byte>   bytes,
                                                                      const std::filesystem::path& identity)
    {
        auto document = detail::readShaderArchiveExpected(bytes, identity);
        if (!document)
        {
            return std::unexpected(document.error());
        }
        auto asset = decodeAsset(*document);
        if (!asset)
        {
            return std::unexpected(asset.error() + ": " + identity.string());
        }
        return asset;
    }

    void ShaderAsset::save(const std::filesystem::path& file) const
    {
        if (auto saved = saveExpected(file); !saved)
        {
            throw std::runtime_error(saved.error());
        }
    }

    ShaderAsset ShaderAsset::load(const std::filesystem::path& file, const AssetSource* source)
    {
        auto asset = loadExpected(file, source);
        if (!asset)
        {
            throw std::runtime_error(asset.error());
        }
        return std::move(*asset);
    }
} // namespace vultra
