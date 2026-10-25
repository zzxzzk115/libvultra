#include <vultra/core/base/property.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <stdexcept>
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

        Json schemaDescription(const PropertySchema& property)
        {
            property.validateDefinition();
            auto defaultContract     = property;
            defaultContract.nullable = true;
            const auto defaultValue  = Json::parse(serializeProperty(defaultContract, property.defaultValue));
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

        void requireMembers(const Json& object, std::initializer_list<std::string_view> keys)
        {
            if (!object.is_object())
            {
                throw std::invalid_argument("Property schema requires an object");
            }
            for (const auto& [key, value] : object.items())
            {
                if (std::ranges::find(keys, key) == keys.end())
                {
                    throw std::invalid_argument("Unknown property schema field: " + key);
                }
            }
        }

        PropertyKind readKind(const Json& value)
        {
            const auto name  = value.get<std::string>();
            const auto found = std::ranges::find(kPropertyKinds, name);
            if (found == kPropertyKinds.end())
            {
                throw std::invalid_argument("Unknown property kind: " + name);
            }
            return PropertyKind(found - kPropertyKinds.begin());
        }

        int64_t readInteger(const Json& value)
        {
            if (!value.is_number_integer() ||
                (value.is_number_unsigned() && value.get<uint64_t>() > uint64_t(INT64_MAX)))
            {
                throw std::invalid_argument("Property schema requires a signed 64-bit integer");
            }
            return value.get<int64_t>();
        }
    } // namespace

    void PropertySchema::validateDefinition() const
    {
        if (name.empty() || jsonPath.empty() || jsonPath.front() != '/' || name.find('\0') != std::string::npos ||
            drawerId.find('\0') != std::string::npos || label.find('\0') != std::string::npos)
        {
            throw std::invalid_argument("Property requires a name and a JSON pointer: " + name);
        }
        try
        {
            static_cast<void>(Json::json_pointer(jsonPath));
        }
        catch (const Json::exception& error)
        {
            throw std::invalid_argument("Invalid property JSON pointer: " + jsonPath + ": " + error.what());
        }
        if (size_t(kind) >= kPropertyKinds.size() || size_t(elementKind) >= kPropertyKinds.size() ||
            (kind == PropertyKind::eArray && elementKind == PropertyKind::eArray) ||
            (integerBits != 0 && integerBits != 8 && integerBits != 16 && integerBits != 32 && integerBits != 64) ||
            (uint8_t(flags) & ~uint8_t(15)) != 0 ||
            (widget != PropertyWidget::eDrag && widget != PropertyWidget::eSlider))
        {
            throw std::invalid_argument("Invalid property type or usage: " + name);
        }
        const auto scalarKind = kind == PropertyKind::eArray ? elementKind : kind;
        if (referenceClass && (scalarKind != PropertyKind::eReference || referenceCategory.empty() ||
                               referenceClass->name.empty() || referenceClass->name.find('\0') != std::string::npos ||
                               referenceClass->module.find('\0') != std::string::npos))
        {
            throw std::invalid_argument("Class constraints require a named reference: " + name);
        }
        if (integerBits != 0 && scalarKind != PropertyKind::eInt && scalarKind != PropertyKind::eUInt)
        {
            throw std::invalid_argument("Only integer properties have an integer width: " + name);
        }
        if (!std::isfinite(min) || !std::isfinite(max) || !std::isfinite(speed) || max < min || speed <= 0)
        {
            throw std::invalid_argument("Invalid property Inspector hints: " + name);
        }
        for (const auto value : enumValues)
        {
            if (std::ranges::count(enumValues, value) != 1)
            {
                throw std::invalid_argument("Duplicate enum value: " + name);
            }
        }
        for (const auto& choice : choices)
        {
            if (std::ranges::find(enumValues, choice.value) == enumValues.end())
            {
                throw std::invalid_argument("Inspector choice has no enum value: " + name);
            }
        }
        if ((kind == PropertyKind::eReference ||
             (kind == PropertyKind::eArray && elementKind == PropertyKind::eReference)) &&
            (referenceType.empty() || referenceCategory.empty() || referenceCategory.find('\0') != std::string::npos))
        {
            throw std::invalid_argument("Reference needs a category and target type: " + name);
        }
        // Required references may be supplied after construction, before lifecycle entry.
        validateKind(kind, defaultValue, true);
    }

    std::string serializeTypeDescription(const ObjectTypeInfo& type)
    {
        Json document {{"version", 1},
                       {"name", type.name},
                       {"base", type.base ? Json(type.base->name) : Json(nullptr)},
                       {"category", type.category},
                       {"properties", Json::array()}};
        for (const auto& property : type.properties)
        {
            document["properties"].push_back(schemaDescription(property));
        }
        return document.dump();
    }

    std::string serializePropertySchema(const PropertySchema& schema)
    {
        return Json {{"version", 1}, {"property", schemaDescription(schema)}}.dump();
    }

    PropertySchema parsePropertySchema(std::string_view text)
    {
        try
        {
            const auto document = Json::parse(text);
            requireMembers(document, {"version", "property"});
            if (!document.at("version").is_number_integer() || document.at("version") != 1)
            {
                throw std::invalid_argument("Property schema requires version 1");
            }
            const auto& data = document.at("property");
            requireMembers(data,
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
            PropertySchema schema;
            schema.name       = data.at("name").get<std::string>();
            schema.label      = data.at("label").is_null() ? "" : data.at("label").get<std::string>();
            schema.drawerId   = data.at("drawer").is_null() ? "" : data.at("drawer").get<std::string>();
            schema.jsonPath   = data.at("json_path").get<std::string>();
            schema.kind       = readKind(data.at("kind"));
            const auto widget = data.at("widget").get<std::string>();
            if (widget != "drag" && widget != "slider")
            {
                throw std::invalid_argument("Unknown property widget: " + widget);
            }
            schema.widget   = widget == "drag" ? PropertyWidget::eDrag : PropertyWidget::eSlider;
            schema.speed    = data.at("speed").get<float>();
            const auto bits = readInteger(data.at("integer_bits"));
            if (bits < 0 || bits > 64)
            {
                throw std::invalid_argument("Invalid property integer width");
            }
            schema.integerBits = uint8_t(bits);
            schema.flags       = PropertyFlags(0);
            const auto& usages = data.at("flags");
            if (!usages.is_array() || !data.at("choices").is_array() || !data.at("enum_values").is_array())
            {
                throw std::invalid_argument("Property flags, choices and enum values require arrays");
            }
            for (const auto& usage : usages)
            {
                const auto name  = usage.get<std::string>();
                const auto found = std::ranges::find(kFlags, name, &std::pair<PropertyFlags, const char*>::second);
                if (found == kFlags.end() || hasPropertyFlag(schema.flags, found->first))
                {
                    throw std::invalid_argument("Unknown/duplicate property flag: " + name);
                }
                schema.flags = schema.flags | found->first;
            }
            for (const auto& value : data.at("enum_values"))
            {
                schema.enumValues.push_back(readInteger(value));
            }
            for (const auto& choice : data.at("choices"))
            {
                requireMembers(choice, {"label", "value"});
                schema.choices.push_back({choice.at("label").get<std::string>(), readInteger(choice.at("value"))});
            }
            if (data.contains("range_hint"))
            {
                const auto& range = data.at("range_hint");
                requireMembers(range, {"min", "max"});
                schema.min = range.at("min").get<double>();
                schema.max = range.at("max").get<double>();
            }
            if (schema.kind == PropertyKind::eArray)
            {
                schema.elementKind = readKind(data.at("element_kind"));
            }
            else if (data.contains("element_kind"))
            {
                throw std::invalid_argument("Only arrays have an element kind");
            }
            const bool reference =
                schema.kind == PropertyKind::eReference ||
                (schema.kind == PropertyKind::eArray && schema.elementKind == PropertyKind::eReference);
            if (reference)
            {
                const auto& target = data.at("reference");
                requireMembers(target, {"category", "type", "nullable", "class"});
                const auto category = target.at("category").get<std::string>();
                if (category.empty() || category.find('\0') != std::string::npos)
                {
                    throw std::invalid_argument("Reference schema requires a category");
                }
                schema.referenceCategory = category;
                schema.referenceType     = target.at("type").get<std::string>();
                schema.nullable          = target.at("nullable").get<bool>();
                if (target.contains("class"))
                {
                    const auto& type = target.at("class");
                    requireMembers(type, {"module", "name"});
                    schema.referenceClass =
                        PropertyClassId {type.at("module").get<std::string>(), type.at("name").get<std::string>()};
                }
            }
            else if (data.contains("reference"))
            {
                throw std::invalid_argument("Only references have a target type");
            }
            auto defaultContract     = schema;
            defaultContract.nullable = true;
            schema.defaultValue      = deserializeProperty(defaultContract, data.at("default").dump());
            schema.validateDefinition();
            return schema;
        }
        catch (const Json::exception& error)
        {
            throw std::invalid_argument(std::string("Invalid property schema: ") + error.what());
        }
    }
} // namespace vultra
