#include <vultra/core/base/property.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace vultra
{
    namespace
    {
        using Json = nlohmann::json;

        void requireFinite(double value, std::string_view property)
        {
            if (!std::isfinite(value))
            {
                throw std::invalid_argument(std::string("Property requires a finite number: ") + std::string(property));
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
                    else if constexpr (std::is_same_v<Value, PropertyReference>)
                    {
                        return item.id.valid() ? Json(item.id.toString()) : Json(nullptr);
                    }
                    else if constexpr (std::is_same_v<Value, PropertyArray>)
                    {
                        auto result = Json::array();
                        for (const auto& element : item.values)
                        {
                            result.push_back(writeValue(element));
                        }
                        return result;
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

        PropertyValue readValue(PropertyKind kind, const Json& value, PropertyKind elementKind)
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
                case PropertyKind::eReference:
                    if (value.is_null())
                    {
                        return PropertyReference {};
                    }
                    if (const auto id = StableId::parse(value.get<std::string>()); id && id->valid())
                    {
                        return PropertyReference {*id};
                    }
                    throw std::invalid_argument("Property requires a persistent UUID or null");
                case PropertyKind::eArray: {
                    if (!value.is_array() || elementKind == PropertyKind::eArray)
                    {
                        throw std::invalid_argument("Property requires a homogeneous array of scalar values");
                    }
                    PropertyArray result;
                    result.values.reserve(value.size());
                    for (const auto& element : value)
                    {
                        result.values.push_back(readValue(elementKind, element, PropertyKind::eBool));
                    }
                    return result;
                }
                case PropertyKind::eVector2:
                case PropertyKind::eVector3:
                case PropertyKind::eVector4:
                case PropertyKind::eMatrix4: {
                    const auto values = value.get<std::vector<float>>();
                    const auto count =
                        kind == PropertyKind::eMatrix4 ? 16 : int(kind) - int(PropertyKind::eVector2) + 2;
                    if (values.size() != size_t(count))
                    {
                        throw std::invalid_argument("Wrong property vector/matrix length");
                    }
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
        if (!object || !get)
        {
            throw std::invalid_argument("Property read requires a native object and getter");
        }
        return get(object);
    }

    void
    PropertySchema::validateKind(PropertyKind expectedKind, const PropertyValue& value, bool declarationDefault) const
    {
        if (value.index() != size_t(expectedKind))
        {
            throw std::invalid_argument(std::string("Wrong value type for property ") + name);
        }
        if (expectedKind == PropertyKind::eReference && !nullable && !declarationDefault &&
            !std::get<PropertyReference>(value).id.valid())
        {
            throw std::invalid_argument(std::string("Property requires a non-null reference: ") + name);
        }
        if (expectedKind == PropertyKind::eArray)
        {
            if (elementKind == PropertyKind::eArray)
            {
                throw std::invalid_argument("Nested property arrays are not supported");
            }
            for (const auto& item : std::get<PropertyArray>(value).values)
            {
                validateKind(elementKind, item, declarationDefault);
            }
        }
        if (expectedKind == PropertyKind::eEnum &&
            std::ranges::find(enumValues, std::get<PropertyEnum>(value).value) == enumValues.end())
        {
            throw std::invalid_argument(std::string("Unknown enum value for property ") + name);
        }
        if (expectedKind == PropertyKind::eInt && integerBits > 0 && integerBits < 64)
        {
            const auto number = std::get<int64_t>(value);
            const auto limit  = int64_t {1} << (integerBits - 1);
            if (number < -limit || number >= limit)
            {
                throw std::invalid_argument(std::string("Integer out of range for property ") + name);
            }
        }
        if (expectedKind == PropertyKind::eUInt && integerBits > 0 && integerBits < 64 &&
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

    void PropertySchema::validate(const PropertyValue& value) const
    {
        validateKind(kind, value, false);
    }

    void PropertyInfo::write(void* object, const PropertyValue& value) const
    {
        if (!object || !set)
        {
            throw std::invalid_argument("Property write requires a native object and setter");
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
        if (!typeInfo.name || typeInfo.name[0] == '\0' || typeInfo.category.empty() ||
            typeInfo.category.find('\0') != std::string::npos)
        {
            throw std::invalid_argument("Property type requires a nonempty name");
        }
        if (typeInfo.base && (std::ranges::find(m_Types, typeInfo.base) == m_Types.end() ||
                              typeInfo.base->category != typeInfo.category))
        {
            throw std::invalid_argument(std::string("Register the matching base category before: ") + typeInfo.name);
        }
        if (std::ranges::any_of(m_Types,
                                [&](const auto* type)
                                {
                                    return type->name == std::string_view(typeInfo.name);
                                }))
        {
            throw std::invalid_argument(std::string("Duplicate property type: ") + typeInfo.name);
        }
        for (const auto& property : typeInfo.properties)
        {
            property.validateDefinition();
            if (!property.get || !property.set)
            {
                throw std::invalid_argument(std::string("Incomplete native property binding: ") + typeInfo.name);
            }
            if (std::ranges::count(typeInfo.properties, std::string_view(property.name), &PropertyInfo::name) != 1)
            {
                throw std::invalid_argument(std::string("Duplicate effective property: ") + property.name);
            }
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
        if (std::ranges::any_of(m_Types,
                                [&](const auto* type)
                                {
                                    return type->base == &typeInfo;
                                }))
        {
            throw std::logic_error(std::string("Remove derived type registrations before: ") + typeInfo.name);
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

    bool ObjectTypeInfo::isA(std::string_view typeName) const
    {
        for (const auto* type = this; type; type = type->base)
        {
            if (type->name == typeName)
            {
                return true;
            }
        }
        return false;
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
                expected[Json::json_pointer(property.jsonPath)] = writeValue(property.defaultValue);
                if ((property.kind == PropertyKind::eArray || property.kind == PropertyKind::eReference) &&
                    input.contains(Json::json_pointer(property.jsonPath)))
                {
                    // Their shape is defined by the descriptor, not by the length/nullness of the default.
                    expected[Json::json_pointer(property.jsonPath)] = input.at(Json::json_pointer(property.jsonPath));
                }
            }
        }
        validateShape(input, expected, type.name);
        std::vector<std::pair<const PropertyInfo*, PropertyValue>> values;
        for (const auto& property : type.properties)
        {
            if (hasPropertyFlag(property.flags, PropertyFlags::eSerialize))
            {
                const Json::json_pointer path(property.jsonPath);
                auto value = input.contains(path) ? readValue(property.kind, input.at(path), property.elementKind) :
                                                    property.defaultValue;
                property.validate(value);
                values.emplace_back(&property, std::move(value));
            }
        }
        for (const auto& [property, value] : values)
        {
            property->write(object, value);
        }
    }

    std::string serializeProperty(const PropertySchema& property, const PropertyValue& value)
    {
        property.validate(value);
        return writeValue(value).dump();
    }

    PropertyValue deserializeProperty(const PropertySchema& property, std::string_view json)
    {
        try
        {
            auto value = readValue(property.kind, Json::parse(json), property.elementKind);
            property.validate(value);
            return value;
        }
        catch (const Json::exception& error)
        {
            throw std::invalid_argument(std::string(property.name) + ": " + error.what());
        }
    }
} // namespace vultra
