#include "support/check.hpp"

#include <vultra/core/base/property.hpp>

#include <nlohmann/json.hpp>

#include <iostream>
#include <limits>
#include <string>

namespace
{
    using namespace vultra;
    using Json = nlohmann::json;
    using support::must;
    using support::require;

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
        auto decoded  = must(parsePropertySchema(must(serializePropertySchema(original))));
        original.name = "discarded";
        original.label.clear();
        require(decoded.name == "speed" && decoded.label == "Speed \xe2\x86\x92" &&
                    decoded.defaultValue == PropertyValue(1.5f),
                "Runtime metadata retained borrowed strings or queried native field accessors");
        auto first  = decoded.defaultValue;
        auto second = decoded.defaultValue;
        first       = must(deserializeProperty(decoded, "2.5"));
        require(first == PropertyValue(2.5f) && second == PropertyValue(1.5f) &&
                    decoded.defaultValue == PropertyValue(1.5f),
                "Independent field values share mutable storage or overwrite declaration defaults");
        require(must(serializePropertySchema(decoded)) == must(serializePropertySchema(speedSchema())),
                "Schema round trip changed UTF-8 or Inspector hints");
        must(decoded.validate(15.0f));
        support::rejects(decoded.validate(true), "Invalid property schema or value was accepted");
        support::rejects(decoded.validate(std::numeric_limits<float>::infinity()),
                         "Invalid property schema or value was accepted");
    }

    void collectionsAndReferences()
    {
        auto schema         = speedSchema();
        schema.kind         = PropertyKind::eArray;
        schema.elementKind  = PropertyKind::eEnum;
        schema.enumValues   = {2, 7};
        schema.choices      = {{"First", 2}, {"Second", 7}};
        schema.defaultValue = PropertyArray {{PropertyEnum {2}, PropertyEnum {7}}};
        auto decoded        = must(parsePropertySchema(must(serializePropertySchema(schema))));
        schema.choices.clear();
        schema.enumValues.clear();
        schema.defaultValue = PropertyArray {};
        require(decoded.choices[1].label == "Second" && decoded.enumValues[1] == 7 &&
                    std::get<PropertyArray>(decoded.defaultValue).values.size() == 2,
                "Schema collections/defaults were borrowed or flattened");
        must(decoded.validate(must(deserializeProperty(decoded, "[7,2,7]"))));
        support::rejects(deserializeProperty(decoded, "[2,99]"), "Invalid property schema or value was accepted");
        schema                   = speedSchema();
        schema.kind              = PropertyKind::eReference;
        schema.referenceType     = "Node3D";
        schema.referenceCategory = "simulation_body";
        schema.nullable          = false;
        schema.defaultValue      = PropertyReference {};
        decoded                  = must(parsePropertySchema(must(serializePropertySchema(schema))));
        require(!decoded.nullable && decoded.referenceType == "Node3D",
                "Required references lost their declaration/instance distinction");
        support::rejects(deserializeProperty(decoded, "null"), "Invalid property schema or value was accepted");
        must(decoded.validate(PropertyReference {StableId::generate()}));
        schema.referenceClass       = PropertyClassId {"project/control", "Beacon"};
        decoded                     = must(parsePropertySchema(must(serializePropertySchema(schema))));
        schema.referenceClass->name = "Discarded";
        require(decoded.referenceClass == PropertyClassId {"project/control", "Beacon"},
                "Behavior class identity was borrowed or lost during schema round trip");
        const auto id = StableId::generate();
        require(must(serializeProperty(decoded, PropertyReference {id})) == Json(id.toString()).dump(),
                "Class-qualified references changed the persistent UUID value encoding");
        decoded.kind         = PropertyKind::eArray;
        decoded.elementKind  = PropertyKind::eReference;
        decoded.defaultValue = PropertyArray {};
        must(decoded.validateDefinition());
        must(decoded.validate(PropertyArray {{PropertyReference {id}}}));
        auto invalidClass = decoded;
        invalidClass.referenceCategory.clear();
        support::rejects(invalidClass.validateDefinition(), "Invalid property schema or value was accepted");
        invalidClass = decoded;
        invalidClass.referenceClass->name.clear();
        support::rejects(invalidClass.validateDefinition(), "Invalid property schema or value was accepted");
        invalidClass                        = decoded;
        invalidClass.referenceClass->module = std::string("a\0b", 3);
        support::rejects(invalidClass.validateDefinition(), "Invalid property schema or value was accepted");
        auto invalidDocument = Json::parse(must(serializePropertySchema(decoded)));
        invalidDocument["property"]["reference"]["class"]["address"] = 1;
        support::rejects(parsePropertySchema(invalidDocument.dump()), "Invalid property schema or value was accepted");
        schema              = speedSchema();
        schema.kind         = PropertyKind::eInt;
        schema.integerBits  = 32;
        schema.defaultValue = int64_t {2};
        decoded             = must(parsePropertySchema(must(serializePropertySchema(schema))));
        support::rejects(deserializeProperty(decoded, "2147483648"), "Invalid property schema or value was accepted");
        schema.kind         = PropertyKind::eMatrix4;
        schema.integerBits  = 0;
        schema.defaultValue = glm::mat4 {1};
        decoded             = must(parsePropertySchema(must(serializePropertySchema(schema))));
        require(must(deserializeProperty(decoded, must(serializeProperty(decoded, decoded.defaultValue)))) ==
                    decoded.defaultValue,
                "Owned matrix schema changed column-major value encoding");
    }

    void invalidDefinitions()
    {
        const auto source     = Json::parse(must(serializePropertySchema(speedSchema())));
        const auto rejectEdit = [&](auto edit)
        {
            auto candidate = source;
            edit(candidate);
            support::rejects(parsePropertySchema(candidate.dump()), "Invalid property schema or value was accepted");
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
{
    ownedMetadata();
    collectionsAndReferences();
    invalidDefinitions();
    std::cout << "Property schema tests passed: owned metadata/defaults, independent values, typed collections, "
                 "references and strict version-1 decoding\n";
    return 0;
}
