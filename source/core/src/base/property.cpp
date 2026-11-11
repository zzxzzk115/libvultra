#include <vultra/core/base/property.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <type_traits>
#include <utility>

namespace vultra
{
    namespace
    {
        using Json = nlohmann::json;

        bool isFinite(const PropertyValue& value)
        {
            return std::visit(
                [](const auto& item)
                {
                    using Value = std::decay_t<decltype(item)>;
                    if constexpr (std::is_floating_point_v<Value>)
                    {
                        return std::isfinite(item);
                    }
                    else if constexpr (std::is_same_v<Value, glm::mat4>)
                    {
                        for (int column = 0; column < 4; ++column)
                        {
                            for (int row = 0; row < 4; ++row)
                            {
                                if (!std::isfinite(item[column][row]))
                                {
                                    return false;
                                }
                            }
                        }
                        return true;
                    }
                    else if constexpr (std::is_same_v<Value, glm::vec2> || std::is_same_v<Value, glm::vec3> ||
                                       std::is_same_v<Value, glm::vec4>)
                    {
                        for (int index = 0; index < item.length(); ++index)
                        {
                            if (!std::isfinite(item[index]))
                            {
                                return false;
                            }
                        }
                        return true;
                    }
                    else
                    {
                        return true;
                    }
                },
                value);
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

        std::expected<int64_t, Error> readSigned(const Json& value)
        {
            if (!value.is_number_integer() ||
                (value.is_number_unsigned() && value.get<uint64_t>() > uint64_t(INT64_MAX)))
            {
                return makeError("Property requires a signed 64-bit integer");
            }
            return value.get<int64_t>();
        }

        // Throws nlohmann::json::exception for mistyped scalars; callers convert it at the parse boundary.
        std::expected<PropertyValue, Error> readValue(PropertyKind kind, const Json& value, PropertyKind elementKind)
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
                        return makeError("Property requires an unsigned 64-bit integer");
                    }
                    return value.get<uint64_t>();
                case PropertyKind::eFloat:
                    return value.get<float>();
                case PropertyKind::eDouble:
                    return value.get<double>();
                case PropertyKind::eString:
                    return value.get<std::string>();
                case PropertyKind::eEnum: {
                    const auto number = readSigned(value);
                    if (!number)
                    {
                        return std::unexpected(number.error());
                    }
                    return PropertyEnum {*number};
                }
                case PropertyKind::eReference:
                    if (value.is_null())
                    {
                        return PropertyReference {};
                    }
                    if (const auto id = StableId::parse(value.get<std::string>()); id && id->valid())
                    {
                        return PropertyReference {*id};
                    }
                    return makeError("Property requires a persistent UUID or null");
                case PropertyKind::eArray: {
                    if (!value.is_array() || elementKind == PropertyKind::eArray)
                    {
                        return makeError("Property requires a homogeneous array of scalar values");
                    }
                    PropertyArray result;
                    result.values.reserve(value.size());
                    for (const auto& element : value)
                    {
                        auto item = readValue(elementKind, element, PropertyKind::eBool);
                        if (!item)
                        {
                            return std::unexpected(item.error());
                        }
                        result.values.push_back(std::move(*item));
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
                        return makeError("Wrong property vector/matrix length");
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
            panic("Unknown property kind");
        }

        std::expected<void, Error> validateShape(const Json& input, const Json& expected, const std::string& path)
        {
            if (expected.is_object())
            {
                if (!input.is_object())
                {
                    return makeError("Expected a property object at {}", path);
                }
                for (const auto& [key, value] : input.items())
                {
                    if (!expected.contains(key))
                    {
                        return makeError("Unknown serialized property at {}/{}", path, key);
                    }
                    if (auto result = validateShape(value, expected.at(key), path + "/" + key); !result)
                    {
                        return result;
                    }
                }
            }
            else if (expected.is_array())
            {
                if (!input.is_array() || input.size() != expected.size())
                {
                    return makeError("Wrong property array length at {}", path);
                }
                for (size_t index = 0; index < input.size(); ++index)
                {
                    if (auto result = validateShape(input[index], expected[index], path + "/" + std::to_string(index));
                        !result)
                    {
                        return result;
                    }
                }
            }
            else if ((expected.is_number() && !input.is_number()) || (expected.is_boolean() && !input.is_boolean()) ||
                     (expected.is_string() && !input.is_string()))
            {
                return makeError("Wrong property value type at {}", path);
            }
            return {};
        }
    } // namespace

    std::expected<PropertyValue, Error> PropertyInfo::read(const void* object) const
    {
        if (!object || !get)
        {
            return makeError("Property read requires a native object and getter");
        }
        return get(object);
    }

    std::expected<void, Error>
    PropertySchema::validateKind(PropertyKind expectedKind, const PropertyValue& value, bool declarationDefault) const
    {
        if (value.index() != size_t(expectedKind))
        {
            return makeError("Wrong value type for property {}", name);
        }
        if (expectedKind == PropertyKind::eReference && !nullable && !declarationDefault &&
            !std::get<PropertyReference>(value).id.valid())
        {
            return makeError("Property requires a non-null reference: {}", name);
        }
        if (expectedKind == PropertyKind::eArray)
        {
            if (elementKind == PropertyKind::eArray)
            {
                return makeError("Nested property arrays are not supported");
            }
            for (const auto& item : std::get<PropertyArray>(value).values)
            {
                if (auto result = validateKind(elementKind, item, declarationDefault); !result)
                {
                    return result;
                }
            }
        }
        if (expectedKind == PropertyKind::eEnum &&
            std::ranges::find(enumValues, std::get<PropertyEnum>(value).value) == enumValues.end())
        {
            return makeError("Unknown enum value for property {}", name);
        }
        if (expectedKind == PropertyKind::eInt && integerBits > 0 && integerBits < 64)
        {
            const auto number = std::get<int64_t>(value);
            const auto limit  = int64_t {1} << (integerBits - 1);
            if (number < -limit || number >= limit)
            {
                return makeError("Integer out of range for property {}", name);
            }
        }
        if (expectedKind == PropertyKind::eUInt && integerBits > 0 && integerBits < 64 &&
            std::get<uint64_t>(value) >= (uint64_t {1} << integerBits))
        {
            return makeError("Integer out of range for property {}", name);
        }
        if (!isFinite(value))
        {
            return makeError("Property requires a finite number: {}", name);
        }
        return {};
    }

    std::expected<void, Error> PropertySchema::validate(const PropertyValue& value) const
    {
        return validateKind(kind, value, false);
    }

    std::expected<void, Error> PropertyInfo::write(void* object, const PropertyValue& value) const
    {
        if (!object || !set)
        {
            return makeError("Property write requires a native object and setter");
        }
        if (auto result = validate(value); !result)
        {
            return result;
        }
        set(object, value);
        return {};
    }

    std::expected<const PropertyInfo*, Error> ObjectTypeInfo::property(std::string_view propertyName) const
    {
        const auto found = std::ranges::find(properties, propertyName, &PropertyInfo::name);
        if (found == properties.end())
        {
            return makeError("{}: unknown property {}", name, propertyName);
        }
        return &*found;
    }

    std::expected<void, Error> ObjectTypeCatalog::add(const ObjectTypeInfo& typeInfo)
    {
        if (!typeInfo.name || typeInfo.name[0] == '\0' || typeInfo.category.empty() ||
            typeInfo.category.find('\0') != std::string::npos)
        {
            return makeError("Property type requires a nonempty name");
        }
        if (typeInfo.base && (std::ranges::find(m_Types, typeInfo.base) == m_Types.end() ||
                              typeInfo.base->category != typeInfo.category))
        {
            return makeError("Register the matching base category before: {}", typeInfo.name);
        }
        if (std::ranges::any_of(m_Types,
                                [&](const auto* type)
                                {
                                    return type->name == std::string_view(typeInfo.name);
                                }))
        {
            return makeError("Duplicate property type: {}", typeInfo.name);
        }
        for (const auto& property : typeInfo.properties)
        {
            if (auto result = property.validateDefinition(); !result)
            {
                return result;
            }
            if (!property.get || !property.set)
            {
                return makeError("Incomplete native property binding: {}", typeInfo.name);
            }
            if (std::ranges::count(typeInfo.properties, std::string_view(property.name), &PropertyInfo::name) != 1)
            {
                return makeError("Duplicate effective property: {}", property.name);
            }
        }
        m_Types.push_back(&typeInfo);
        return {};
    }

    std::expected<void, Error> ObjectTypeCatalog::remove(const ObjectTypeInfo& typeInfo)
    {
        const auto found = std::ranges::find(m_Types, &typeInfo);
        if (found == m_Types.end())
        {
            return makeError("Property type does not belong to this catalog: {}", typeInfo.name);
        }
        if (std::ranges::any_of(m_Types,
                                [&](const auto* type)
                                {
                                    return type->base == &typeInfo;
                                }))
        {
            return makeError("Remove derived type registrations before: {}", typeInfo.name);
        }
        m_Types.erase(found);
        return {};
    }

    std::expected<const ObjectTypeInfo*, Error> ObjectTypeCatalog::type(std::string_view name) const
    {
        const auto found = std::ranges::find_if(m_Types,
                                                [&](const auto* type)
                                                {
                                                    return type->name == name;
                                                });
        if (found == m_Types.end())
        {
            return makeError("Unknown property type: {}", name);
        }
        return *found;
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

    std::expected<std::string, Error> serializeProperties(const ObjectTypeInfo& type, const void* object)
    {
        if (!object)
        {
            return makeError("Cannot encode a null property object");
        }
        auto json = Json::object();
        for (const auto& property : type.properties)
        {
            if (hasPropertyFlag(property.flags, PropertyFlags::eSerialize))
            {
                const auto value = property.read(object);
                if (!value)
                {
                    return std::unexpected(value.error());
                }
                if (auto result = property.validate(*value); !result)
                {
                    return std::unexpected(result.error());
                }
                try
                {
                    json[Json::json_pointer(property.jsonPath)] = writeValue(*value);
                }
                catch (const Json::exception& error)
                {
                    return makeError("{}: {}", property.name, error.what());
                }
            }
        }
        return json.dump();
    }

    std::expected<void, Error> deserializeProperties(const ObjectTypeInfo& type, std::string_view json, void* object)
    {
        if (!object)
        {
            return makeError("Cannot decode a null property object");
        }
        std::vector<std::pair<const PropertyInfo*, PropertyValue>> values;
        try
        {
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
                        expected[Json::json_pointer(property.jsonPath)] =
                            input.at(Json::json_pointer(property.jsonPath));
                    }
                }
            }
            if (auto result = validateShape(input, expected, type.name); !result)
            {
                return result;
            }
            for (const auto& property : type.properties)
            {
                if (hasPropertyFlag(property.flags, PropertyFlags::eSerialize))
                {
                    const Json::json_pointer            path(property.jsonPath);
                    std::expected<PropertyValue, Error> value = property.defaultValue;
                    if (input.contains(path))
                    {
                        value = readValue(property.kind, input.at(path), property.elementKind);
                    }
                    if (!value)
                    {
                        return std::unexpected(value.error());
                    }
                    if (auto result = property.validate(*value); !result)
                    {
                        return result;
                    }
                    values.emplace_back(&property, std::move(*value));
                }
            }
        }
        catch (const Json::exception& error)
        {
            return makeError("{}: {}", type.name, error.what());
        }
        for (const auto& [property, value] : values)
        {
            if (auto result = property->write(object, value); !result)
            {
                return result;
            }
        }
        return {};
    }

    std::expected<std::string, Error> serializeProperty(const PropertySchema& property, const PropertyValue& value)
    {
        if (auto result = property.validate(value); !result)
        {
            return std::unexpected(result.error());
        }
        return writeValue(value).dump();
    }

    std::expected<PropertyValue, Error> deserializeProperty(const PropertySchema& property, std::string_view json)
    {
        try
        {
            auto value = readValue(property.kind, Json::parse(json), property.elementKind);
            if (!value)
            {
                return value;
            }
            if (auto result = property.validate(*value); !result)
            {
                return std::unexpected(result.error());
            }
            return value;
        }
        catch (const Json::exception& error)
        {
            return makeError("{}: {}", property.name, error.what());
        }
    }
} // namespace vultra
