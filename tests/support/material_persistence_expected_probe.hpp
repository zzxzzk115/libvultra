#pragma once

#include <vultra/assets/material_instance.hpp>

#include <cstdio>

namespace vultra::test
{
    inline std::expected<void, std::string> materialPersistenceExpectedProbe()
    {
        const auto        id     = StableId::generate().toString();
        const auto        prefix = "{\"version\":1,\"shader\":\"" + id + "\",\"variant\":\"Detail:V\",\"properties\":";
        const std::string properties = R"({
            "gain":{"type":0,"value":2.5},
            "mode":{"type":1,"value":-2147483648},
            "enabled":{"type":2,"value":true},
            "color":{"type":3,"value":[0.25,0.5,0.75,1]},
            "texture":{"type":4,"value":{"asset":null,"builtin":"white","scale_offset":[2,3,0.25,0.5]}}
        })";
        auto              instance   = MaterialInstance::parseExpected(prefix + properties + "}");
        if (!instance || instance->overrides().size() != 5 || instance->revision() != 0 ||
            instance->variant != "Detail:V" || instance->shader.value.toString() != id ||
            std::get<float>(instance->overrides().at("gain")) != 2.5f ||
            std::get<int32_t>(instance->overrides().at("mode")) != INT32_MIN ||
            !std::get<bool>(instance->overrides().at("enabled")))
        {
            return std::unexpected("Expected material persistence lost typed values, identity or initial revision");
        }
        auto text = instance->serializeExpected();
        if (!text)
        {
            return std::unexpected(text.error());
        }
        auto restored = MaterialInstance::parseExpected(*text);
        if (!restored || restored->overrides() != instance->overrides() || restored->variant != instance->variant ||
            restored->shader != instance->shader || restored->revision() != 0 || restored->serializeExpected() != text)
        {
            return std::unexpected("Expected material persistence changed typed roundtrip bytes");
        }
        const auto retained = instance->overrides();
        for (const auto* invalid : {"", "[]", "null", "{", "{\"version\":2}", "{\"version\":1,\"shader\":3}"})
        {
            if (MaterialInstance::parseExpected(invalid))
            {
                return std::unexpected("Expected material persistence accepted malformed JSON/schema");
            }
        }
        for (const auto* value :
             {R"({"x":{"type":0,"value":"bad"}})",
              R"({"x":{"type":0,"value":1e400}})",
              R"({"x":{"type":1,"value":2147483648}})",
              R"({"x":{"type":1,"value":1.5}})",
              R"({"x":{"type":2,"value":1}})",
              R"({"x":{"type":3,"value":[1,2,3]}})",
              R"({"x":{"type":3,"value":[1,2,3,"bad"]}})",
              R"({"x":{"type":-1,"value":1}})",
              R"({"x":{"type":9,"value":1}})",
              R"({"":{"type":0,"value":1}})",
              R"({"x":{"type":4,"value":{"asset":"invalid","builtin":"","scale_offset":[1,1,0,0]}}})",
              R"({"x":{"type":4,"value":{"asset":null,"builtin":"unknown","scale_offset":[1,1,0,0]}}})"})
        {
            if (MaterialInstance::parseExpected(prefix + value + "}") || instance->overrides() != retained)
            {
                return std::unexpected("Rejected material property changed the accepted instance");
            }
        }
        auto invalid    = *instance;
        invalid.variant = std::string(1, char(0xff));
        if (invalid.serializeExpected())
        {
            return std::unexpected("Material serialization replaced an invalid UTF-8 variant");
        }
        invalid = *instance;
        invalid.variant.clear();
        if (invalid.serializeExpected())
        {
            return std::unexpected("Material serialization accepted an empty variant");
        }
        invalid        = *instance;
        invalid.shader = {};
        if (invalid.serializeExpected() || MaterialInstance {}.serializeExpected() ||
            instance->overrides() != retained || instance->serializeExpected() != text)
        {
            return std::unexpected("Material serialization accepted invalid identity or failed recovery");
        }
        std::puts("Expected material persistence: five typed values, byte-stable JSON, schema/value/UTF-8 rejection "
                  "and accepted-instance retention passed");
        return {};
    }
} // namespace vultra::test
