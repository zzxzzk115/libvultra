#include "shaders/shader_values.hpp"

#include <cmath>
#include <limits>
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

        std::expected<ShaderPropertyValue, std::string> decodePropertyValueExpected(const nlohmann::json& document)
        {
            const auto type  = document.is_object() ? document.find("type") : document.end();
            const auto value = document.is_object() ? document.find("value") : document.end();
            if (type == document.end() || !type->is_number_unsigned() || type->get<uint64_t>() > 4 ||
                value == document.end())
            {
                return std::unexpected("Invalid material property value type");
            }
            const auto finiteFloat = [](const nlohmann::json& number) -> std::expected<float, std::string>
            {
                if (!number.is_number())
                {
                    return std::unexpected("Expected a finite material number");
                }
                const auto result = number.get<double>();
                if (!std::isfinite(result) || std::abs(result) > std::numeric_limits<float>::max())
                {
                    return std::unexpected("Non-finite material number");
                }
                return float(result);
            };
            const auto vector = [&](const nlohmann::json& array) -> std::expected<std::array<float, 4>, std::string>
            {
                if (!array.is_array() || array.size() != 4)
                {
                    return std::unexpected("Expected four material vector components");
                }
                std::array<float, 4> result;
                for (size_t index = 0; index < result.size(); ++index)
                {
                    auto component = finiteFloat(array[index]);
                    if (!component)
                    {
                        return std::unexpected(component.error());
                    }
                    result[index] = *component;
                }
                return result;
            };
            switch (type->get<uint32_t>())
            {
                case 0: {
                    auto number = finiteFloat(*value);
                    if (!number)
                    {
                        return std::unexpected(number.error());
                    }
                    return *number;
                }
                case 1: {
                    if (!value->is_number_integer() || value->get<double>() < INT32_MIN ||
                        value->get<double>() > INT32_MAX)
                    {
                        return std::unexpected("Invalid material integer");
                    }
                    return value->get<int32_t>();
                }
                case 2:
                    if (!value->is_boolean())
                    {
                        return std::unexpected("Invalid material boolean");
                    }
                    return value->get<bool>();
                case 3: {
                    auto components = vector(*value);
                    if (!components)
                    {
                        return std::unexpected(components.error());
                    }
                    return *components;
                }
                case 4: {
                    if (!value->is_object())
                    {
                        return std::unexpected("Invalid material texture value");
                    }
                    const auto asset       = value->find("asset");
                    const auto builtin     = value->find("builtin");
                    const auto scaleOffset = value->find("scale_offset");
                    if (asset == value->end() || builtin == value->end() || !builtin->is_string() ||
                        scaleOffset == value->end())
                    {
                        return std::unexpected("Invalid material texture fields");
                    }
                    ShaderTextureValue texture;
                    if (!asset->is_null())
                    {
                        if (!asset->is_string())
                        {
                            return std::unexpected("Invalid material texture asset ID");
                        }
                        const auto id = StableId::parse(asset->get_ref<const std::string&>());
                        if (!id)
                        {
                            return std::unexpected("Invalid material texture asset ID");
                        }
                        texture.asset = {*id};
                    }
                    texture.builtin = builtin->get_ref<const std::string&>();
                    auto components = vector(*scaleOffset);
                    if (!components)
                    {
                        return std::unexpected(components.error());
                    }
                    texture.scaleOffset = *components;
                    ShaderProperty property;
                    property.type = ShaderPropertyType::eTexture2D;
                    if (auto valid = property.validateExpected(texture); !valid)
                    {
                        return std::unexpected(valid.error());
                    }
                    return texture;
                }
            }
            return std::unexpected("Invalid material property value type");
        }

        ShaderPropertyValue decodePropertyValue(const nlohmann::json& document)
        {
            auto value = decodePropertyValueExpected(document);
            if (!value)
            {
                throw std::invalid_argument(value.error());
            }
            return std::move(*value);
        }
    } // namespace detail

    const std::map<std::string, ShaderPropertyValue, std::less<>>& MaterialInstance::overrides() const
    {
        return m_Overrides;
    }

    void MaterialInstance::reset(std::string_view name)
    {
        if (auto reset = resetExpected(name); !reset)
        {
            throw std::invalid_argument(reset.error());
        }
    }

    std::expected<void, std::string> MaterialInstance::resetExpected(std::string_view name)
    {
        const auto found = m_Overrides.find(name);
        if (found == m_Overrides.end())
        {
            return {};
        }
        if (m_Revision == UINT64_MAX)
        {
            return std::unexpected("Material revision exhausted");
        }
        m_Overrides.erase(found);
        ++m_Revision;
        return {};
    }

    uint64_t MaterialInstance::revision() const
    {
        return m_Revision;
    }

    std::expected<std::string, std::string> MaterialInstance::serializeExpected() const
    {
        if (!shader.value.valid())
        {
            return std::unexpected("Persistent material requires a shader AssetId");
        }
        if (variant.empty() || variant.size() > 4096 || variant.find(char(0)) != std::string::npos ||
            m_Overrides.size() > 4096)
        {
            return std::unexpected("Invalid material variant or property count");
        }
        nlohmann::json document {{"version", 1},
                                 {"shader", shader.value.toString()},
                                 {"variant", variant},
                                 {"properties", nlohmann::json::object()}};
        constexpr std::array types {ShaderPropertyType::eFloat,
                                    ShaderPropertyType::eInteger,
                                    ShaderPropertyType::eBoolean,
                                    ShaderPropertyType::eVector,
                                    ShaderPropertyType::eTexture2D};
        for (const auto& [name, value] : m_Overrides)
        {
            if (name.empty() || name.size() > 4096 || name.find(char(0)) != std::string::npos ||
                value.index() >= types.size())
            {
                return std::unexpected("Invalid material property name/value");
            }
            ShaderProperty property;
            property.type = types[value.index()];
            if (auto valid = property.validateExpected(value); !valid)
            {
                return std::unexpected("Material property " + name + ": " + valid.error());
            }
            document["properties"][name] = detail::encodePropertyValue(value);
        }
        // Do not replace invalid UTF-8 and silently change a persistent identifier or value.
        auto text = document.dump(2, ' ', false, nlohmann::json::error_handler_t::replace) + "\n";
        if (text.size() > 64 * 1024 * 1024 || nlohmann::json::parse(text, nullptr, false) != document)
        {
            return std::unexpected("Material persistence requires valid UTF-8 and at most 64 MiB");
        }
        return text;
    }

    std::expected<MaterialInstance, std::string> MaterialInstance::parseExpected(std::string_view text)
    {
        if (text.size() > 64 * 1024 * 1024)
        {
            return std::unexpected("Material instance exceeds 64 MiB");
        }
        const auto document = nlohmann::json::parse(text, nullptr, false);
        if (!document.is_object())
        {
            return std::unexpected("Invalid material instance JSON object");
        }
        const auto version    = document.find("version");
        const auto shader     = document.find("shader");
        const auto variant    = document.find("variant");
        const auto properties = document.find("properties");
        if (version == document.end() || !version->is_number_integer() || *version != 1 || shader == document.end() ||
            !shader->is_string() || variant == document.end() || !variant->is_string() ||
            properties == document.end() || !properties->is_object() || properties->size() > 4096)
        {
            return std::unexpected("Unsupported material instance format or field types");
        }
        MaterialInstance instance;
        const auto       id = StableId::parse(shader->get_ref<const std::string&>());
        if (!id || !id->valid())
        {
            return std::unexpected("Invalid material shader AssetId");
        }
        instance.shader  = {*id};
        instance.variant = variant->get_ref<const std::string&>();
        if (instance.variant.empty() || instance.variant.size() > 4096 ||
            instance.variant.find(char(0)) != std::string::npos)
        {
            return std::unexpected("Invalid material variant");
        }
        for (const auto& [name, value] : properties->items())
        {
            if (name.empty() || name.size() > 4096 || name.find(char(0)) != std::string::npos)
            {
                return std::unexpected("Invalid material property name");
            }
            auto decoded = detail::decodePropertyValueExpected(value);
            if (!decoded)
            {
                return std::unexpected("Material property " + name + ": " + decoded.error());
            }
            instance.m_Overrides.emplace(name, std::move(*decoded));
        }
        return instance;
    }

    std::string MaterialInstance::serialize() const
    {
        auto text = serializeExpected();
        if (!text)
        {
            throw std::invalid_argument(text.error());
        }
        return std::move(*text);
    }

    MaterialInstance MaterialInstance::parse(std::string_view text)
    {
        auto instance = parseExpected(text);
        if (!instance)
        {
            throw std::invalid_argument(instance.error());
        }
        return std::move(*instance);
    }
} // namespace vultra
