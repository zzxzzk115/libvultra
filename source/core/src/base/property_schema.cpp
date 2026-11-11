#include <vultra/core/base/property.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <utility>

namespace vultra
{
    namespace
    {
        using Json = nlohmann::json;

        constexpr std::array kPropertyKinds {"bool",
                                             "int",
                                             "uint",
                                             "float",
                                             "double",
                                             "string",
                                             "enum",
                                             "vector2",
                                             "vector3",
                                             "vector4",
                                             "matrix4",
                                             "reference",
                                             "array"};
        constexpr std::array kFlags {std::pair {PropertyFlags::eSerialize, "serialize"},
                                     std::pair {PropertyFlags::eInspect, "inspect"},
                                     std::pair {PropertyFlags::eBind, "bind"},
                                     std::pair {PropertyFlags::eReload, "reload"}};

        std::expected<Json, Error> schemaDescription(const PropertySchema& property)
        {
            if (auto result = property.validateDefinition(); !result)
            {
                return std::unexpected(result.error());
            }
            auto defaultContract     = property;
            defaultContract.nullable = true;
            const auto defaultText   = serializeProperty(defaultContract, property.defaultValue);
            if (!defaultText)
            {
                return std::unexpected(defaultText.error());
            }
            const auto defaultValue = Json::parse(*defaultText);
            Json       description {{"name", property.name},
                                    {"label", property.label.empty() ? Json(nullptr) : Json(property.label)},
                                    {"kind", kPropertyKinds.at(size_t(property.kind))},
                                    {"json_path", property.jsonPath},
                                    {"flags", Json::array()},
                                    {"default", defaultValue},
                                    {"drawer", property.drawerId.empty() ? Json(nullptr) : Json(property.drawerId)},
                                    {"widget", property.widget == PropertyWidget::eDrag ? "drag" : "slider"},
                                    {"speed", property.speed},
                                    {"integer_bits", property.integerBits},
                                    {"choices", Json::array()},
                                    {"enum_values", property.enumValues}};
            for (const auto& [flag, name] : kFlags)
            {
                if (hasPropertyFlag(property.flags, flag))
                {
                    description["flags"].push_back(name);
                }
            }
            if (property.min != 0 || property.max != 0)
            {
                description["range_hint"] = {{"min", property.min}, {"max", property.max}};
            }
            for (const auto& choice : property.choices)
            {
                description["choices"].push_back({{"label", choice.label}, {"value", choice.value}});
            }
            if (property.kind == PropertyKind::eArray)
            {
                description["element_kind"] = kPropertyKinds.at(size_t(property.elementKind));
            }
            if (property.kind == PropertyKind::eReference ||
                (property.kind == PropertyKind::eArray && property.elementKind == PropertyKind::eReference))
            {
                description["reference"] = {{"category", property.referenceCategory},
                                            {"type", property.referenceType},
                                            {"nullable", property.nullable}};
                if (property.referenceClass)
                {
                    description["reference"]["class"] = {{"module", property.referenceClass->module},
                                                         {"name", property.referenceClass->name}};
                }
            }
            return description;
        }

        std::expected<void, Error> requireMembers(const Json& object, std::initializer_list<std::string_view> keys)
        {
            if (!object.is_object())
            {
                return makeError("Property schema requires an object");
            }
            for (const auto& [key, value] : object.items())
            {
                if (std::ranges::find(keys, key) == keys.end())
                {
                    return makeError("Unknown property schema field: {}", key);
                }
            }
            return {};
        }

        // Throws nlohmann::json::exception for a non-string; parsePropertySchema converts it.
        std::expected<PropertyKind, Error> readKind(const Json& value)
        {
            const auto name  = value.get<std::string>();
            const auto found = std::ranges::find(kPropertyKinds, name);
            if (found == kPropertyKinds.end())
            {
                return makeError("Unknown property kind: {}", name);
            }
            return PropertyKind(found - kPropertyKinds.begin());
        }

        std::expected<int64_t, Error> readInteger(const Json& value)
        {
            if (!value.is_number_integer() ||
                (value.is_number_unsigned() && value.get<uint64_t>() > uint64_t(INT64_MAX)))
            {
                return makeError("Property schema requires a signed 64-bit integer");
            }
            return value.get<int64_t>();
        }

        std::expected<PropertySchema, Error> readSchema(const Json& document)
        {
            if (auto result = requireMembers(document, {"version", "property"}); !result)
            {
                return std::unexpected(result.error());
            }
            if (!document.at("version").is_number_integer() || document.at("version") != 1)
            {
                return makeError("Property schema requires version 1");
            }
            const auto& data = document.at("property");
            if (auto result = requireMembers(data,
                                             {"name",
                                              "label",
                                              "kind",
                                              "json_path",
                                              "flags",
                                              "default",
                                              "drawer",
                                              "widget",
                                              "speed",
                                              "integer_bits",
                                              "choices",
                                              "enum_values",
                                              "range_hint",
                                              "element_kind",
                                              "reference"});
                !result)
            {
                return std::unexpected(result.error());
            }
            PropertySchema schema;
            schema.name     = data.at("name").get<std::string>();
            schema.label    = data.at("label").is_null() ? "" : data.at("label").get<std::string>();
            schema.drawerId = data.at("drawer").is_null() ? "" : data.at("drawer").get<std::string>();
            schema.jsonPath = data.at("json_path").get<std::string>();
            const auto kind = readKind(data.at("kind"));
            if (!kind)
            {
                return std::unexpected(kind.error());
            }
            schema.kind       = *kind;
            const auto widget = data.at("widget").get<std::string>();
            if (widget != "drag" && widget != "slider")
            {
                return makeError("Unknown property widget: {}", widget);
            }
            schema.widget   = widget == "drag" ? PropertyWidget::eDrag : PropertyWidget::eSlider;
            schema.speed    = data.at("speed").get<float>();
            const auto bits = readInteger(data.at("integer_bits"));
            if (!bits)
            {
                return std::unexpected(bits.error());
            }
            if (*bits < 0 || *bits > 64)
            {
                return makeError("Invalid property integer width");
            }
            schema.integerBits = uint8_t(*bits);
            schema.flags       = PropertyFlags(0);
            const auto& usages = data.at("flags");
            if (!usages.is_array() || !data.at("choices").is_array() || !data.at("enum_values").is_array())
            {
                return makeError("Property flags, choices and enum values require arrays");
            }
            for (const auto& usage : usages)
            {
                const auto name  = usage.get<std::string>();
                const auto found = std::ranges::find(kFlags, name, &std::pair<PropertyFlags, const char*>::second);
                if (found == kFlags.end() || hasPropertyFlag(schema.flags, found->first))
                {
                    return makeError("Unknown/duplicate property flag: {}", name);
                }
                schema.flags = schema.flags | found->first;
            }
            for (const auto& value : data.at("enum_values"))
            {
                const auto number = readInteger(value);
                if (!number)
                {
                    return std::unexpected(number.error());
                }
                schema.enumValues.push_back(*number);
            }
            for (const auto& choice : data.at("choices"))
            {
                if (auto result = requireMembers(choice, {"label", "value"}); !result)
                {
                    return std::unexpected(result.error());
                }
                const auto value = readInteger(choice.at("value"));
                if (!value)
                {
                    return std::unexpected(value.error());
                }
                schema.choices.push_back({choice.at("label").get<std::string>(), *value});
            }
            if (data.contains("range_hint"))
            {
                const auto& range = data.at("range_hint");
                if (auto result = requireMembers(range, {"min", "max"}); !result)
                {
                    return std::unexpected(result.error());
                }
                schema.min = range.at("min").get<double>();
                schema.max = range.at("max").get<double>();
            }
            if (schema.kind == PropertyKind::eArray)
            {
                const auto elementKind = readKind(data.at("element_kind"));
                if (!elementKind)
                {
                    return std::unexpected(elementKind.error());
                }
                schema.elementKind = *elementKind;
            }
            else if (data.contains("element_kind"))
            {
                return makeError("Only arrays have an element kind");
            }
            const bool reference =
                schema.kind == PropertyKind::eReference ||
                (schema.kind == PropertyKind::eArray && schema.elementKind == PropertyKind::eReference);
            if (reference)
            {
                const auto& target = data.at("reference");
                if (auto result = requireMembers(target, {"category", "type", "nullable", "class"}); !result)
                {
                    return std::unexpected(result.error());
                }
                const auto category = target.at("category").get<std::string>();
                if (category.empty() || category.find('\0') != std::string::npos)
                {
                    return makeError("Reference schema requires a category");
                }
                schema.referenceCategory = category;
                schema.referenceType     = target.at("type").get<std::string>();
                schema.nullable          = target.at("nullable").get<bool>();
                if (target.contains("class"))
                {
                    const auto& type = target.at("class");
                    if (auto result = requireMembers(type, {"module", "name"}); !result)
                    {
                        return std::unexpected(result.error());
                    }
                    schema.referenceClass =
                        PropertyClassId {type.at("module").get<std::string>(), type.at("name").get<std::string>()};
                }
            }
            else if (data.contains("reference"))
            {
                return makeError("Only references have a target type");
            }
            auto defaultContract     = schema;
            defaultContract.nullable = true;
            auto defaultValue        = deserializeProperty(defaultContract, data.at("default").dump());
            if (!defaultValue)
            {
                return std::unexpected(defaultValue.error());
            }
            schema.defaultValue = std::move(*defaultValue);
            if (auto result = schema.validateDefinition(); !result)
            {
                return std::unexpected(result.error());
            }
            return schema;
        }
    } // namespace

    std::expected<void, Error> PropertySchema::validateDefinition() const
    {
        if (name.empty() || jsonPath.empty() || jsonPath.front() != '/' || name.find('\0') != std::string::npos ||
            drawerId.find('\0') != std::string::npos || label.find('\0') != std::string::npos)
        {
            return makeError("Property requires a name and a JSON pointer: {}", name);
        }
        try
        {
            static_cast<void>(Json::json_pointer(jsonPath));
        }
        catch (const Json::exception& error)
        {
            return makeError("Invalid property JSON pointer: {}: {}", jsonPath, error.what());
        }
        if (size_t(kind) >= kPropertyKinds.size() || size_t(elementKind) >= kPropertyKinds.size() ||
            (kind == PropertyKind::eArray && elementKind == PropertyKind::eArray) ||
            (integerBits != 0 && integerBits != 8 && integerBits != 16 && integerBits != 32 && integerBits != 64) ||
            (uint8_t(flags) & ~uint8_t(15)) != 0 ||
            (widget != PropertyWidget::eDrag && widget != PropertyWidget::eSlider))
        {
            return makeError("Invalid property type or usage: {}", name);
        }
        const auto scalarKind = kind == PropertyKind::eArray ? elementKind : kind;
        if (referenceClass && (scalarKind != PropertyKind::eReference || referenceCategory.empty() ||
                               referenceClass->name.empty() || referenceClass->name.find('\0') != std::string::npos ||
                               referenceClass->module.find('\0') != std::string::npos))
        {
            return makeError("Class constraints require a named reference: {}", name);
        }
        if (integerBits != 0 && scalarKind != PropertyKind::eInt && scalarKind != PropertyKind::eUInt)
        {
            return makeError("Only integer properties have an integer width: {}", name);
        }
        if (!std::isfinite(min) || !std::isfinite(max) || !std::isfinite(speed) || max < min || speed <= 0)
        {
            return makeError("Invalid property Inspector hints: {}", name);
        }
        for (const auto value : enumValues)
        {
            if (std::ranges::count(enumValues, value) != 1)
            {
                return makeError("Duplicate enum value: {}", name);
            }
        }
        for (const auto& choice : choices)
        {
            if (std::ranges::find(enumValues, choice.value) == enumValues.end())
            {
                return makeError("Inspector choice has no enum value: {}", name);
            }
        }
        if ((kind == PropertyKind::eReference ||
             (kind == PropertyKind::eArray && elementKind == PropertyKind::eReference)) &&
            (referenceType.empty() || referenceCategory.empty() || referenceCategory.find('\0') != std::string::npos))
        {
            return makeError("Reference needs a category and target type: {}", name);
        }
        // Required references may be supplied after construction, before lifecycle entry.
        return validateKind(kind, defaultValue, true);
    }

    std::expected<std::string, Error> serializeTypeDescription(const ObjectTypeInfo& type)
    {
        Json document {{"version", 1},
                       {"name", type.name},
                       {"base", type.base ? Json(type.base->name) : Json(nullptr)},
                       {"category", type.category},
                       {"properties", Json::array()}};
        for (const auto& property : type.properties)
        {
            auto description = schemaDescription(property);
            if (!description)
            {
                return std::unexpected(description.error());
            }
            document["properties"].push_back(std::move(*description));
        }
        return document.dump();
    }

    std::expected<std::string, Error> serializePropertySchema(const PropertySchema& schema)
    {
        auto description = schemaDescription(schema);
        if (!description)
        {
            return std::unexpected(description.error());
        }
        return Json {{"version", 1}, {"property", std::move(*description)}}.dump();
    }

    std::expected<PropertySchema, Error> parsePropertySchema(std::string_view text)
    {
        try
        {
            return readSchema(Json::parse(text));
        }
        catch (const Json::exception& error)
        {
            return makeError("Invalid property schema: {}", error.what());
        }
    }
} // namespace vultra
