#include <vultra/core/base/property.hpp>

#include <nlohmann/json.hpp>

#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace
{
    using namespace vultra;
    using Json = nlohmann::json;

    void require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    template<class Callback>
    void reject(Callback&& callback)
    {
        try
        {
            callback();
        }
        catch (const std::invalid_argument&)
        {
            return;
        }
        throw std::runtime_error("Invalid property schema or value was accepted");
    }

    PropertySchema speedSchema()
    {
        PropertySchema schema;
        schema.name         = "speed";
        schema.label        = "Speed \xe2\x86\x92";
        schema.drawerId     = "Script.speed";
        schema.jsonPath     = "/motion/speed";
        schema.kind         = PropertyKind::eFloat;
        schema.min          = 0;
        schema.max          = 10;
        schema.speed        = 0.1f;
        schema.defaultValue = 1.5f;
        return schema;
    }

    void ownedMetadata()
    {
        auto original = speedSchema();
        auto decoded  = parsePropertySchema(serializePropertySchema(original));
        original.name = "discarded";
        original.label.clear();
        require(decoded.name == "speed" && decoded.label == "Speed \xe2\x86\x92" &&
                    decoded.defaultValue == PropertyValue(1.5f),
                "Runtime metadata retained borrowed strings or queried native field accessors");
        auto first  = decoded.defaultValue;
        auto second = decoded.defaultValue;
        first       = deserializeProperty(decoded, "2.5");
        require(first == PropertyValue(2.5f) && second == PropertyValue(1.5f) &&
                    decoded.defaultValue == PropertyValue(1.5f),
                "Independent field values share mutable storage or overwrite declaration defaults");
        require(serializePropertySchema(decoded) == serializePropertySchema(speedSchema()),
                "Schema round trip changed UTF-8 or Inspector hints");
        decoded.validate(15.0f);
        reject(
            [&]
            {
                decoded.validate(true);
            });
        reject(
            [&]
            {
                decoded.validate(std::numeric_limits<float>::infinity());
            });
    }

    void collectionsAndReferences()
    {
        auto schema         = speedSchema();
        schema.kind         = PropertyKind::eArray;
        schema.elementKind  = PropertyKind::eEnum;
        schema.enumValues   = {2, 7};
        schema.choices      = {{"First", 2}, {"Second", 7}};
        schema.defaultValue = PropertyArray {{PropertyEnum {2}, PropertyEnum {7}}};
        auto decoded        = parsePropertySchema(serializePropertySchema(schema));
        schema.choices.clear();
        schema.enumValues.clear();
        schema.defaultValue = PropertyArray {};
        require(decoded.choices[1].label == "Second" && decoded.enumValues[1] == 7 &&
                    std::get<PropertyArray>(decoded.defaultValue).values.size() == 2,
                "Schema collections/defaults were borrowed or flattened");
        decoded.validate(deserializeProperty(decoded, "[7,2,7]"));
        reject(
            [&]
            {
                deserializeProperty(decoded, "[2,99]");
            });
        schema                   = speedSchema();
        schema.kind              = PropertyKind::eReference;
        schema.referenceType     = "Node3D";
        schema.referenceCategory = "simulation_body";
        schema.nullable          = false;
        schema.defaultValue      = PropertyReference {};
        decoded                  = parsePropertySchema(serializePropertySchema(schema));
        require(!decoded.nullable && decoded.referenceType == "Node3D",
                "Required references lost their declaration/instance distinction");
        reject(
            [&]
            {
                deserializeProperty(decoded, "null");
            });
        decoded.validate(PropertyReference {StableId::generate()});
        schema.referenceClass       = PropertyClassId {"project/control", "Beacon"};
        decoded                     = parsePropertySchema(serializePropertySchema(schema));
        schema.referenceClass->name = "Discarded";
        require(decoded.referenceClass == PropertyClassId {"project/control", "Beacon"},
                "Behavior class identity was borrowed or lost during schema round trip");
        const auto id = StableId::generate();
        require(serializeProperty(decoded, PropertyReference {id}) == Json(id.toString()).dump(),
                "Class-qualified references changed the persistent UUID value encoding");
        decoded.kind         = PropertyKind::eArray;
        decoded.elementKind  = PropertyKind::eReference;
        decoded.defaultValue = PropertyArray {};
        decoded.validateDefinition();
        decoded.validate(PropertyArray {{PropertyReference {id}}});
        auto invalidClass = decoded;
        invalidClass.referenceCategory.clear();
        reject(
            [&]
            {
                invalidClass.validateDefinition();
            });
        invalidClass = decoded;
        invalidClass.referenceClass->name.clear();
        reject(
            [&]
            {
                invalidClass.validateDefinition();
            });
        invalidClass                        = decoded;
        invalidClass.referenceClass->module = std::string("a\0b", 3);
        reject(
            [&]
            {
                invalidClass.validateDefinition();
            });
        auto invalidDocument                                         = Json::parse(serializePropertySchema(decoded));
        invalidDocument["property"]["reference"]["class"]["address"] = 1;
        reject(
            [&]
            {
                parsePropertySchema(invalidDocument.dump());
            });
        schema              = speedSchema();
        schema.kind         = PropertyKind::eInt;
        schema.integerBits  = 32;
        schema.defaultValue = int64_t {2};
        decoded             = parsePropertySchema(serializePropertySchema(schema));
        reject(
            [&]
            {
                deserializeProperty(decoded, "2147483648");
            });
        schema.kind         = PropertyKind::eMatrix4;
        schema.integerBits  = 0;
        schema.defaultValue = glm::mat4 {1};
        decoded             = parsePropertySchema(serializePropertySchema(schema));
        require(deserializeProperty(decoded, serializeProperty(decoded, decoded.defaultValue)) == decoded.defaultValue,
                "Owned matrix schema changed column-major value encoding");
    }

    void invalidDefinitions()
    {
        const auto source     = Json::parse(serializePropertySchema(speedSchema()));
        const auto rejectEdit = [&](auto edit)
        {
            auto candidate = source;
            edit(candidate);
            reject(
                [&]
                {
                    parsePropertySchema(candidate.dump());
                });
        };
        rejectEdit(
            [](Json& value)
            {
                value["version"] = 2;
            });
        rejectEdit(
            [](Json& value)
            {
                value["version"] = true;
            });
        rejectEdit(
            [](Json& value)
            {
                value["property"]["getter"] = "native address";
            });
        rejectEdit(
            [](Json& value)
            {
                value["property"]["kind"] = "object";
            });
        rejectEdit(
            [](Json& value)
            {
                value["property"]["default"] = "wrong type";
            });
        rejectEdit(
            [](Json& value)
            {
                value["property"]["flags"] = {"bind", "bind"};
            });
        rejectEdit(
            [](Json& value)
            {
                value["property"]["flags"] = {"unknown"};
            });
        rejectEdit(
            [](Json& value)
            {
                value["property"]["json_path"] = "/bad~2escape";
            });
        rejectEdit(
            [](Json& value)
            {
                value["property"]["integer_bits"] = -4294967232LL;
            });
        rejectEdit(
            [](Json& value)
            {
                value["property"]["speed"] = 0;
            });
        rejectEdit(
            [](Json& value)
            {
                value["property"]["element_kind"] = "float";
            });
        rejectEdit(
            [](Json& value)
            {
                value["property"]["choices"] = {{{"label", "Invalid"}, {"value", 9}}};
            });
        rejectEdit(
            [](Json& value)
            {
                value["property"]["enum_values"] = {2, 2};
            });
        rejectEdit(
            [](Json& value)
            {
                value["property"]["reference"] = {{"category", "component"}};
            });
    }
} // namespace

int main()
try
{
    ownedMetadata();
    collectionsAndReferences();
    invalidDefinitions();
    std::cout << "Property schema tests passed: owned metadata/defaults, independent values, typed collections, "
                 "references and strict version-1 decoding\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
