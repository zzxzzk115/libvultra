#include "shaders/shader_values.hpp"

#include <cmath>
#include <stdexcept>

namespace vultra
{
    namespace detail
    {
        nlohmann::json encodePropertyValue(const ShaderPropertyValue& value)
        {
            nlohmann::json document {{"type", value.index()}};
            if (const auto* scalar = std::get_if<float>(&value))
            {
                document["value"] = *scalar;
            }
            else if (const auto* integer = std::get_if<int32_t>(&value))
            {
                document["value"] = *integer;
            }
            else if (const auto* boolean = std::get_if<bool>(&value))
            {
                document["value"] = *boolean;
            }
            else if (const auto* vector = std::get_if<std::array<float, 4>>(&value))
            {
                document["value"] = *vector;
            }
            else
            {
                const auto& texture = std::get<ShaderTextureValue>(value);
                document["value"]   = {{"asset",
                                        texture.asset.value.valid() ? nlohmann::json(texture.asset.value.toString()) :
                                                                      nlohmann::json(nullptr)},
                                       {"builtin", texture.builtin},
                                       {"scale_offset", texture.scaleOffset}};
            }
            return document;
        }

        ShaderPropertyValue decodePropertyValue(const nlohmann::json& document)
        {
            const auto  type  = document.at("type").get<uint32_t>();
            const auto& value = document.at("value");
            switch (type)
            {
                case 0: {
                    const auto number = value.get<float>();
                    if (!std::isfinite(number))
                    {
                        throw std::invalid_argument("Non-finite material float");
                    }
                    return number;
                }
                case 1: {
                    if (!value.is_number_integer() || value.get<double>() < INT32_MIN ||
                        value.get<double>() > INT32_MAX)
                    {
                        throw std::invalid_argument("Invalid material integer");
                    }
                    return value.get<int32_t>();
                }
                case 2:
                    return value.get<bool>();
                case 3: {
                    const auto vector = value.get<std::array<float, 4>>();
                    for (const auto component : vector)
                    {
                        if (!std::isfinite(component))
                        {
                            throw std::invalid_argument("Non-finite material vector");
                        }
                    }
                    return vector;
                }
                case 4: {
                    ShaderTextureValue texture;
                    if (!value.at("asset").is_null())
                    {
                        const auto id = StableId::parse(value.at("asset").get<std::string>());
                        if (!id)
                        {
                            throw std::invalid_argument("Invalid material texture asset ID");
                        }
                        texture.asset = {*id};
                    }
                    texture.builtin     = value.at("builtin").get<std::string>();
                    texture.scaleOffset = value.at("scale_offset").get<std::array<float, 4>>();
                    ShaderProperty property;
                    property.type = ShaderPropertyType::eTexture2D;
                    property.validate(texture);
                    return texture;
                }
                default:
                    throw std::invalid_argument("Invalid material property value type");
            }
        }
    } // namespace detail

    const std::map<std::string, ShaderPropertyValue, std::less<>>& MaterialInstance::overrides() const
    {
        return m_Overrides;
    }

    void MaterialInstance::reset(std::string_view name)
    {
        if (m_Overrides.erase(std::string(name)))
        {
            ++m_Revision;
        }
    }

    uint64_t MaterialInstance::revision() const
    {
        return m_Revision;
    }

    std::string MaterialInstance::serialize() const
    {
        if (!shader.value.valid())
        {
            throw std::invalid_argument("Persistent material requires a shader AssetId");
        }
        nlohmann::json document {{"version", 1},
                                 {"shader", shader.value.toString()},
                                 {"variant", variant},
                                 {"properties", nlohmann::json::object()}};
        for (const auto& [name, value] : m_Overrides)
        {
            document["properties"][name] = detail::encodePropertyValue(value);
        }
        return document.dump(2) + "\n";
    }

    MaterialInstance MaterialInstance::parse(std::string_view text)
    {
        const auto document = nlohmann::json::parse(text);
        if (document.at("version") != 1 || !document.at("properties").is_object() ||
            document.at("properties").size() > 4096)
        {
            throw std::invalid_argument("Unsupported material instance format");
        }
        MaterialInstance instance;
        const auto       id = StableId::parse(document.at("shader").get<std::string>());
        if (!id)
        {
            throw std::invalid_argument("Invalid material shader AssetId");
        }
        instance.shader  = {*id};
        instance.variant = document.at("variant").get<std::string>();
        if (instance.variant.empty() || instance.variant.size() > 4096)
        {
            throw std::invalid_argument("Invalid material variant");
        }
        for (const auto& [name, value] : document.at("properties").items())
        {
            if (name.empty() || name.size() > 4096)
            {
                throw std::invalid_argument("Invalid material property name");
            }
            instance.m_Overrides.emplace(name, detail::decodePropertyValue(value));
        }
        return instance;
    }
} // namespace vultra
