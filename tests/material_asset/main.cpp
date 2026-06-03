#include <vultra/function/material/material_asset.hpp>

#include <cstdlib>
#include <iostream>
#include <string_view>
#include <variant>

using namespace vultra::material;

namespace
{
    void require(bool condition, std::string_view message)
    {
        if (condition)
            return;
        std::cerr << "material_asset test failed: " << message << '\n';
        std::exit(1);
    }

    bool hasDiagnostic(const MaterialAssetParseResult& result, std::string_view text)
    {
        for (const auto& diagnostic : result.diagnostics)
            if (diagnostic.find(text) != std::string::npos)
                return true;
        return false;
    }
} // namespace

int main()
{
    {
        const auto result = loadMaterialAssetFromText(R"json({
            "type": "Material",
            "version": 1,
            "name": "Default",
            "source": {"kind": "builtin", "id": "builtin/pbr"},
            "properties": {
                "baseColor": [1.0, 0.5, 0.25, 1.0],
                "metallic": 0.0,
                "roughness": 0.5,
                "doubleSided": true,
                "baseColorTexture": "res://textures/albedo.png"
            }
        })json");
        require(result.ok(), "valid builtin material should parse");
        require(result.asset.source.kind == MaterialSourceKind::eBuiltin, "builtin source kind should parse");
        require(result.asset.source.id == "builtin/pbr", "builtin source id should parse");
        require(std::holds_alternative<glm::vec4>(result.asset.properties.at("baseColor")),
                "vec4 property should parse");
        require(std::holds_alternative<float>(result.asset.properties.at("roughness")),
                "float property should parse");
        require(std::holds_alternative<bool>(result.asset.properties.at("doubleSided")),
                "bool property should parse");
        require(std::holds_alternative<std::string>(result.asset.properties.at("baseColorTexture")),
                "texture URI property should parse as string");
    }

    {
        const auto result = loadMaterialAssetFromText(R"json({
            "type": "Material",
            "source": {"kind": "graph", "uri": "res://materials/stylized.vmatgraph.json"},
            "properties": {"tint": [1.0, 0.0, 0.0, 1.0]}
        })json");
        require(result.ok(), "valid graph material should parse");
        require(result.asset.source.kind == MaterialSourceKind::eGraph, "graph source kind should parse");
        require(result.asset.source.uri == "res://materials/stylized.vmatgraph.json", "graph URI should parse");
    }

    {
        const auto result = loadMaterialAssetFromText(R"json({
            "type": "Material",
            "source": {"kind": "shader", "id": "materials/custom_surface.frag"},
            "properties": {}
        })json");
        require(result.ok(), "valid single shader material should parse");
        require(result.asset.source.kind == MaterialSourceKind::eShader, "shader source kind should parse");
        require(result.asset.source.shaderLibrary == "project", "shader source should default to project library");
    }

    {
        const auto result = loadMaterialAssetFromText(R"json({
            "type": "Material",
            "version": -1,
            "source": {"kind": "builtin", "id": "builtin/pbr"}
        })json");
        require(!result.ok(), "negative material version should fail");
        require(hasDiagnostic(result, "version must be positive"), "negative version diagnostic should be clear");
    }

    {
        const auto result = loadMaterialAssetFromText(R"json({
            "type": "Material",
            "source": {"kind": "pipeline", "id": "legacy"},
            "properties": {}
        })json");
        require(!result.ok(), "unknown material source kind should fail");
        require(hasDiagnostic(result, "Unknown material source.kind"), "unknown kind diagnostic should be clear");
        require(!hasDiagnostic(result, "Builtin material source requires id"),
                "unknown kind should not emit builtin-specific diagnostics");
    }

    {
        const auto result = loadMaterialAssetFromText(R"json({
            "type": "Material",
            "properties": {}
        })json");
        require(!result.ok(), "missing material source should fail");
        require(hasDiagnostic(result, "source must be an object"), "missing source diagnostic should be clear");
    }

    {
        const auto result = loadMaterialAssetFromText(R"json({
            "type": "Material",
            "source": {"kind": "builtin", "id": "builtin/pbr"},
            "properties": {"badColor": [1.0, "red", 0.0, 1.0]}
        })json");
        require(!result.ok(), "unsupported property value should fail");
        require(hasDiagnostic(result, "badColor"), "property diagnostic should include the property name");
    }

    std::cout << "material_asset tests passed\n";
    return 0;
}
