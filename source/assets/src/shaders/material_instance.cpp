#include "shader_values.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
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

    ShaderPropertyValue MaterialInstance::value(const ShaderAsset& asset, std::string_view name) const
    {
        const auto& property = asset.property(name);
        const auto  found    = m_Overrides.find(name);
        if (found != m_Overrides.end())
        {
            return found->second;
        }
        return property.defaultValue;
    }

    void MaterialInstance::set(const ShaderAsset& asset, std::string_view name, ShaderPropertyValue value)
    {
        const auto& property = asset.property(name);
        property.validate(value);
        if (const auto found = m_Overrides.find(name); found != m_Overrides.end() && found->second == value)
        {
            return;
        }
        if (value == property.defaultValue)
        {
            reset(name);
            return;
        }
        m_Overrides.insert_or_assign(std::string(name), std::move(value));
        ++m_Revision;
    }

    void MaterialInstance::reset(std::string_view name)
    {
        if (m_Overrides.erase(std::string(name)))
        {
            ++m_Revision;
        }
    }

    void MaterialInstance::setVariant(const ShaderAsset& asset, std::string value)
    {
        asset.variant(value);
        if (value != variant)
        {
            variant = std::move(value);
            ++m_Revision;
        }
    }

    void MaterialInstance::validate(const ShaderAsset& asset) const
    {
        asset.variant(variant);
        for (const auto& [name, value] : m_Overrides)
        {
            asset.property(name).validate(value);
        }
        for (const auto& property : asset.properties)
        {
            if (property.isTexture())
            {
                const auto texture = std::get<ShaderTextureValue>(value(asset, property.name));
                if (!texture.asset.value.valid() && texture.builtin.empty())
                {
                    throw std::invalid_argument(property.location.describe() +
                                                ": material requires texture: " + property.name);
                }
            }
        }
    }

    void
    MaterialInstance::reconcile(const ShaderAsset& previous, const ShaderAsset& candidate, std::string& diagnostics)
    {
        for (const auto& property : previous.properties)
        {
            const auto found = std::ranges::find(candidate.properties, property.name, &ShaderProperty::name);
            if (found == candidate.properties.end())
            {
                diagnostics += "Removed material property: " + property.name + "\n";
            }
            else if (found->type != property.type)
            {
                diagnostics += "Reset type-changed material property: " + property.name + "\n";
            }
        }
        for (const auto& property : candidate.properties)
        {
            if (std::ranges::find(previous.properties, property.name, &ShaderProperty::name) ==
                previous.properties.end())
            {
                diagnostics += "Added material property with shader default: " + property.name + "\n";
            }
        }
        std::erase_if(m_Overrides,
                      [&](const auto& override)
                      {
                          const auto found = std::ranges::find_if(candidate.properties,
                                                                  [&](const ShaderProperty& property)
                                                                  {
                                                                      return property.name == override.first;
                                                                  });
                          const auto old   = std::ranges::find_if(previous.properties,
                                                                  [&](const ShaderProperty& property)
                                                                  {
                                                                    return property.name == override.first;
                                                                  });
                          if (found == candidate.properties.end() || old == previous.properties.end() ||
                              found->type != old->type)
                          {
                              return true;
                          }
                          found->validate(override.second);
                          return false;
                      });
        candidate.variant(variant);
        validate(candidate);
        ++m_Revision;
    }

    void MaterialInstance::assign(const ShaderAsset& asset, const MaterialInstance& source)
    {
        source.validate(asset);
        if (shader != source.shader || variant != source.variant || m_Overrides != source.m_Overrides)
        {
            auto overrides = source.m_Overrides;
            auto selected  = source.variant;
            m_Overrides.swap(overrides);
            variant.swap(selected);
            shader = source.shader;
            ++m_Revision;
        }
    }

    uint64_t MaterialInstance::revision() const
    {
        return m_Revision;
    }

    std::vector<std::byte> MaterialInstance::uniformData(const ShaderAsset& asset, const ShaderProgram& program) const
    {
        validate(asset);
        const auto* block = program.parameters.field("material");
        if (!block || block->kind != ShaderParameterKind::eParameterBlock)
        {
            throw std::invalid_argument("Program does not expose the generated material ParameterBlock");
        }
        const auto* uniforms = block->field("$element");
        if (!uniforms || uniforms->size > 16 * 1024 * 1024)
        {
            throw std::invalid_argument("Invalid reflected material uniform size");
        }
        std::vector<std::byte> bytes(size_t(uniforms->size));
        auto                   write = [&](std::string_view name, const void* data, size_t size)
        {
            const auto* field = uniforms->field(name);
            if (!field || field->size < size)
            {
                throw std::invalid_argument("Material field does not match reflection: " + std::string(name));
            }
            const auto offset = field->offset(ShaderOffsetKind::eUniform);
            if (offset > bytes.size() || size > bytes.size() - offset)
            {
                throw std::invalid_argument("Material field lies outside the uniform block: " + std::string(name));
            }
            std::memcpy(bytes.data() + offset, data, size);
        };
        for (const auto& property : asset.properties)
        {
            const auto propertyValue = value(asset, property.name);
            if (const auto* scalar = std::get_if<float>(&propertyValue))
            {
                write(property.name, scalar, sizeof(*scalar));
            }
            else if (const auto* integer = std::get_if<int32_t>(&propertyValue))
            {
                write(property.name, integer, sizeof(*integer));
            }
            else if (const auto* boolean = std::get_if<bool>(&propertyValue))
            {
                const uint32_t integer = *boolean ? 1 : 0;
                write(property.name, &integer, sizeof(integer));
            }
            else if (const auto* vector = std::get_if<std::array<float, 4>>(&propertyValue))
            {
                auto components = *vector;
                if (property.type == ShaderPropertyType::eColor && !property.hdr)
                {
                    for (size_t index = 0; index < 3; ++index)
                    {
                        const auto component = components[index];
                        components[index] =
                            component <= 0.04045f ? component / 12.92f : std::pow((component + 0.055f) / 1.055f, 2.4f);
                    }
                }
                write(property.name, components.data(), sizeof(components));
            }
            else if (!property.noScaleOffset && (property.type == ShaderPropertyType::eTexture2D ||
                                                 property.type == ShaderPropertyType::eTexture2DArray))
            {
                const auto& texture = std::get<ShaderTextureValue>(propertyValue);
                write(property.name + "ScaleOffset", texture.scaleOffset.data(), sizeof(texture.scaleOffset));
            }
        }
        return bytes;
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
