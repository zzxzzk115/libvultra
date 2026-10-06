#include <vultra/core/base/property.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <type_traits>

namespace vultra
{
    namespace
    {
        using Json = nlohmann::json;

        void requireFinite(double value, const char* property)
        {
            if (!std::isfinite(value))
            {
                throw std::invalid_argument(std::string("Property requires a finite number: ") + property);
            }
        }

        Json writeValue(const PropertyValue& value)
        {
            return std::visit(
                [](const auto& item) -> Json
                {
                    using Value = std::decay_t<decltype(item)>;
                    if constexpr (std::is_same_v<Value, PropertyEnum>)
                    {
                        return item.value;
                    }
                    else if constexpr (std::is_same_v<Value, glm::mat4>)
                    {
                        auto result = Json::array();
                        for (int column = 0; column < 4; ++column)
                        {
                            for (int row = 0; row < 4; ++row)
                            {
                                result.push_back(item[column][row]);
                            }
                        }
                        return result;
                    }
                    else if constexpr (std::is_same_v<Value, glm::vec2> || std::is_same_v<Value, glm::vec3> ||
                                       std::is_same_v<Value, glm::vec4>)
                    {
                        auto result = Json::array();
                        for (int index = 0; index < item.length(); ++index)
                        {
                            result.push_back(item[index]);
                        }
                        return result;
                    }
                    else
                    {
                        return item;
                    }
                },
                value);
        }

        int64_t readSigned(const Json& value)
        {
            if (!value.is_number_integer() ||
                (value.is_number_unsigned() && value.get<uint64_t>() > uint64_t(INT64_MAX)))
            {
                throw std::invalid_argument("Property requires a signed 64-bit integer");
            }
            return value.get<int64_t>();
        }

        PropertyValue readValue(PropertyKind kind, const Json& value)
        {
            switch (kind)
            {
                case PropertyKind::eBool:
                    return value.get<bool>();
                case PropertyKind::eInt:
                    return readSigned(value);
                case PropertyKind::eUInt:
                    if (!value.is_number_integer() || (!value.is_number_unsigned() && value.get<int64_t>() < 0))
                    {
                        throw std::invalid_argument("Property requires an unsigned 64-bit integer");
                    }
                    return value.get<uint64_t>();
                case PropertyKind::eFloat:
                    return value.get<float>();
                case PropertyKind::eDouble:
                    return value.get<double>();
                case PropertyKind::eString:
                    return value.get<std::string>();
                case PropertyKind::eEnum:
                    return PropertyEnum {readSigned(value)};
                case PropertyKind::eVector2:
                case PropertyKind::eVector3:
                case PropertyKind::eVector4:
                case PropertyKind::eMatrix4: {
                    const auto values = value.get<std::vector<float>>();
                    switch (kind)
                    {
                        case PropertyKind::eVector2:
                            return glm::vec2 {values.at(0), values.at(1)};
                        case PropertyKind::eVector3:
                            return glm::vec3 {values.at(0), values.at(1), values.at(2)};
                        case PropertyKind::eVector4:
                            return glm::vec4 {values.at(0), values.at(1), values.at(2), values.at(3)};
                        default: {
                            glm::mat4 matrix;
                            for (int column = 0; column < 4; ++column)
                            {
                                for (int row = 0; row < 4; ++row)
                                {
                                    matrix[column][row] = values.at(size_t(column * 4 + row));
                                }
                            }
                            return matrix;
                        }
                    }
                }
            }
            throw std::logic_error("Unknown property kind");
        }

        void validateShape(const Json& input, const Json& expected, const std::string& path)
        {
            if (expected.is_object())
            {
                if (!input.is_object())
                {
                    throw std::invalid_argument("Expected a property object at " + path);
                }
                for (const auto& [key, value] : input.items())
                {
                    if (!expected.contains(key))
                    {
                        throw std::invalid_argument("Unknown serialized property at " + path + "/" + key);
                    }
                    validateShape(value, expected.at(key), path + "/" + key);
                }
            }
            else if (expected.is_array())
            {
                if (!input.is_array() || input.size() != expected.size())
                {
                    throw std::invalid_argument("Wrong property array length at " + path);
                }
                for (size_t index = 0; index < input.size(); ++index)
                {
                    validateShape(input[index], expected[index], path + "/" + std::to_string(index));
                }
            }
            else if ((expected.is_number() && !input.is_number()) || (expected.is_boolean() && !input.is_boolean()) ||
                     (expected.is_string() && !input.is_string()))
            {
                throw std::invalid_argument("Wrong property value type at " + path);
            }
        }
    } // namespace

    PropertyValue PropertyInfo::read(const void* object) const
    {
        if (!object)
        {
            throw std::invalid_argument("Cannot read a null property object");
        }
        return get(object);
    }

    void PropertyInfo::validate(const PropertyValue& value) const
    {
        if (value.index() != size_t(kind))
        {
            throw std::invalid_argument(std::string("Wrong value type for property ") + name);
        }
        if (kind == PropertyKind::eEnum &&
            std::ranges::find(enumValues, std::get<PropertyEnum>(value).value) == enumValues.end())
        {
            throw std::invalid_argument(std::string("Unknown enum value for property ") + name);
        }
        if (kind == PropertyKind::eInt && integerBits < 64)
        {
            const auto number = std::get<int64_t>(value);
            const auto limit  = int64_t {1} << (integerBits - 1);
            if (number < -limit || number >= limit)
            {
                throw std::invalid_argument(std::string("Integer out of range for property ") + name);
            }
        }
        if (kind == PropertyKind::eUInt && integerBits < 64 &&
            std::get<uint64_t>(value) >= (uint64_t {1} << integerBits))
        {
            throw std::invalid_argument(std::string("Integer out of range for property ") + name);
        }
        std::visit(
            [this](const auto& item)
            {
                using Value = std::decay_t<decltype(item)>;
                if constexpr (std::is_floating_point_v<Value>)
                {
                    requireFinite(item, name);
                }
                else if constexpr (std::is_same_v<Value, glm::mat4>)
                {
                    for (int column = 0; column < 4; ++column)
                    {
                        for (int row = 0; row < 4; ++row)
                        {
                            requireFinite(item[column][row], name);
                        }
                    }
                }
                else if constexpr (std::is_same_v<Value, glm::vec2> || std::is_same_v<Value, glm::vec3> ||
                                   std::is_same_v<Value, glm::vec4>)
                {
                    for (int index = 0; index < item.length(); ++index)
                    {
                        requireFinite(item[index], name);
                    }
                }
            },
            value);
    }

    void PropertyInfo::write(void* object, const PropertyValue& value) const
    {
        if (!object)
        {
            throw std::invalid_argument("Cannot write a null property object");
        }
        validate(value);
        set(object, value);
    }

    const PropertyInfo& ObjectTypeInfo::property(std::string_view propertyName) const
    {
        const auto found = std::ranges::find(properties, propertyName, &PropertyInfo::name);
        if (found == properties.end())
        {
            throw std::invalid_argument(std::string(name) + ": unknown property " + std::string(propertyName));
        }
        return *found;
    }

    void ObjectTypeCatalog::add(const ObjectTypeInfo& typeInfo)
    {
        if (!typeInfo.name || typeInfo.name[0] == '\0')
        {
            throw std::invalid_argument("Property type requires a nonempty name");
        }
        if (std::ranges::any_of(m_Types,
                                [&](const auto* type)
                                {
                                    return type->name == std::string_view(typeInfo.name);
                                }))
        {
            throw std::invalid_argument(std::string("Duplicate property type: ") + typeInfo.name);
        }
        m_Types.push_back(&typeInfo);
    }

    void ObjectTypeCatalog::remove(const ObjectTypeInfo& typeInfo)
    {
        const auto found = std::ranges::find(m_Types, &typeInfo);
        if (found == m_Types.end())
        {
            throw std::invalid_argument(std::string("Property type does not belong to this catalog: ") + typeInfo.name);
        }
        m_Types.erase(found);
    }

    const ObjectTypeInfo& ObjectTypeCatalog::type(std::string_view name) const
    {
        const auto found = std::ranges::find_if(m_Types,
                                                [&](const auto* type)
                                                {
                                                    return type->name == name;
                                                });
        if (found == m_Types.end())
        {
            throw std::invalid_argument("Unknown property type: " + std::string(name));
        }
        return **found;
    }

    std::span<const ObjectTypeInfo* const> ObjectTypeCatalog::types() const
    {
        return m_Types;
    }

    std::string serializeProperties(const ObjectTypeInfo& type, const void* object)
    {
        if (!object)
        {
            throw std::invalid_argument("Cannot encode a null property object");
        }
        auto json = Json::object();
        for (const auto& property : type.properties)
        {
            if (hasPropertyFlag(property.flags, PropertyFlags::eSerialize))
            {
                const auto value = property.read(object);
                property.validate(value);
                json[Json::json_pointer(property.jsonPath)] = writeValue(value);
            }
        }
        return json.dump();
    }

    void deserializeProperties(const ObjectTypeInfo& type, std::string_view json, void* object)
    {
        if (!object)
        {
            throw std::invalid_argument("Cannot decode a null property object");
        }
        const auto input    = Json::parse(json);
        auto       expected = Json::object();
        for (const auto& property : type.properties)
        {
            if (hasPropertyFlag(property.flags, PropertyFlags::eSerialize))
            {
                expected[Json::json_pointer(property.jsonPath)] = writeValue(property.defaultValue());
            }
        }
        validateShape(input, expected, type.name);
        std::vector<std::pair<const PropertyInfo*, PropertyValue>> values;
        for (const auto& property : type.properties)
        {
            if (hasPropertyFlag(property.flags, PropertyFlags::eSerialize))
            {
                const Json::json_pointer path(property.jsonPath);
                auto value = input.contains(path) ? readValue(property.kind, input.at(path)) : property.defaultValue();
                property.validate(value);
                values.emplace_back(&property, std::move(value));
            }
        }
        for (const auto& [property, value] : values)
        {
            property->write(object, value);
        }
    }
} // namespace vultra
