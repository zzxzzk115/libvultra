#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace vultra
{
    enum class MaterialGraphSource
    {
        eParameter,
        eNode,
        eLiteral
    };

    struct MaterialGraphValue
    {
        MaterialGraphSource source = MaterialGraphSource::eNode;
        std::string         name;
        std::string         selection; // Relative struct/vector members and constant array indices.
        std::variant<float, int32_t, uint32_t, bool> literal = 0.0f;
    };

    struct MaterialGraphParameter
    {
        std::string name;
        std::string type; // Ordinary Slang type; the compiler checks calls and inferred intermediate types.
    };

    struct MaterialGraphNode
    {
        std::string                     id;
        std::string                     function;
        std::vector<MaterialGraphValue> arguments;
    };

    // CPU authoring data. Generates an ordinary Slang value function; owns no GPU bindings or runtime executor.
    struct MaterialGraph
    {
        std::string                         entry;
        std::string                         resultType;
        std::vector<MaterialGraphParameter> parameters;
        std::string code; // Authored Slang helpers/imports. Node functions should express value computations.
        std::vector<MaterialGraphNode> nodes;
        MaterialGraphValue             result;

        // Validates every link/cycle, then emits only the result's dependency closure in stable ID order.
        // Slang checks parameter/result types, member/index selections, overloads and function implementations.
        std::expected<std::string, std::string>
        sourceExpected(const std::filesystem::path& identity = "material_graph") const;
        std::expected<std::string, std::string>          serializeExpected() const;
        static std::expected<MaterialGraph, std::string> parseExpected(std::string_view text);
    };
} // namespace vultra
