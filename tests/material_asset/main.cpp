#include <vultra/function/material/material_asset.hpp>

#include <vshadersystem/system.hpp>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
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

    bool nearlyEqual(const float a, const float b) { return std::abs(a - b) < 0.0001f; }

    bool hasDiagnostic(const MaterialAssetParseResult& result, std::string_view text)
    {
        for (const auto& diagnostic : result.diagnostics)
            if (diagnostic.find(text) != std::string::npos)
                return true;
        return false;
    }

    template<typename T>
    vshadersystem::ParamDefault shaderDefault(const vshadersystem::ParamType type, const T& value)
    {
        vshadersystem::ParamDefault out;
        out.type = type;
        std::memcpy(out.valueBuffer, &value, sizeof(T));
        return out;
    }

    std::string readTextFile(const std::filesystem::path& path)
    {
        std::ifstream file(path);
        return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }

    std::filesystem::path findRepoRoot()
    {
        auto hasMeshMaterialInclude = [](const std::filesystem::path& dir) {
            return std::filesystem::exists(dir / "builtin/shaders/include/vultra/mesh_material.glsl");
        };

        auto scanUp = [&](std::filesystem::path dir) -> std::filesystem::path {
            dir = std::filesystem::absolute(dir);
            while (!dir.empty())
            {
                if (hasMeshMaterialInclude(dir))
                    return dir;
                const auto parent = dir.parent_path();
                if (parent == dir)
                    break;
                dir = parent;
            }
            return {};
        };

        if (auto root = scanUp(std::filesystem::current_path()); !root.empty())
            return root;
        return scanUp(std::filesystem::path(__FILE__).parent_path());
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
        std::vector<vultra::material_graph::Diagnostic> diagnostics;
        const auto graph = vultra::material_graph::loadGraphFromText(R"json({
            "type": "MaterialGraph",
            "version": 1,
            "name": "Blackboard Probe",
            "blackboard": [
                {
                    "name": "tint",
                    "type": "color",
                    "default": [0.25, 0.5, 0.75, 1.0],
                    "displayName": "Tint",
                    "ui": {"min": 0.0, "max": 1.0}
                },
                {
                    "name": "roughness",
                    "type": "float",
                    "default": 0.62,
                    "displayName": "Roughness",
                    "ui": {"min": 0.05, "max": 1.0}
                },
                {
                    "name": "albedoTex",
                    "type": "texture2D",
                    "default": "res://textures/default.png",
                    "displayName": "Albedo"
                }
            ],
            "nodes": [],
            "links": []
        })json",
                                                                 &diagnostics);
        require(graph.has_value(), "material graph with blackboard should parse");
        require(diagnostics.empty(), "blackboard graph should not emit diagnostics");
        require(graph->blackboard.size() == 3, "graph blackboard params should load");

        const auto schema = materialSourceSchemaFromGraph(*graph);
        require(schema.parameters.size() == 3, "graph blackboard should convert to material schema");
        require(schema.parameters[0].name == "tint", "graph color param name should convert");
        require(schema.parameters[0].type == MaterialPropertyType::eColor, "graph color param type should convert");
        require(schema.parameters[0].displayName == "Tint", "graph display name should convert");
        require(std::holds_alternative<glm::vec4>(schema.parameters[0].defaultValue),
                "graph color default should convert");
        const auto tintDefault = std::get<glm::vec4>(schema.parameters[0].defaultValue);
        require(nearlyEqual(tintDefault.x, 0.25f) && nearlyEqual(tintDefault.y, 0.5f) &&
                    nearlyEqual(tintDefault.z, 0.75f) && nearlyEqual(tintDefault.w, 1.0f),
                "graph color default values should convert");

        require(schema.parameters[1].name == "roughness", "graph float param name should convert");
        require(schema.parameters[1].type == MaterialPropertyType::eFloat, "graph float param type should convert");
        require(nearlyEqual(std::get<float>(schema.parameters[1].defaultValue), 0.62f),
                "graph float default should convert");
        require(schema.parameters[1].hasUiRange && nearlyEqual(schema.parameters[1].uiMin, 0.05f) &&
                    nearlyEqual(schema.parameters[1].uiMax, 1.0f),
                "graph float UI range should convert");

        require(schema.parameters[2].name == "albedoTex", "graph texture param name should convert");
        require(schema.parameters[2].type == MaterialPropertyType::eTexture2D,
                "graph texture param type should convert");
        require(std::get<std::string>(schema.parameters[2].defaultValue) == "res://textures/default.png",
                "graph texture default should convert");

        const auto saved = vultra::material_graph::saveGraphToText(*graph);
        require(saved.find("\"blackboard\"") != std::string::npos, "graph save should preserve blackboard");
        require(saved.find("\"typeId\"") != std::string::npos || graph->nodes.empty(),
                "graph save should use canonical typeId for nodes when present");
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
            "source": {"kind": "shader", "shaderLibrary": "builtin", "id": "direct_gbuffer.frag"},
            "properties": {}
        })json");
        require(result.ok(), "valid builtin-library shader material should parse");
        require(result.asset.source.shaderLibrary == "builtin", "shader source should preserve explicit library");
    }

    {
        vshadersystem::MaterialDescription desc;
        desc.params.push_back(vshadersystem::MaterialParamDesc {
            .name         = "roughness",
            .type         = vshadersystem::ParamType::eFloat,
            .offset       = 0,
            .size         = 4,
            .semantic     = vshadersystem::Semantic::eRoughness,
            .hasDefault   = true,
            .defaultValue = shaderDefault(vshadersystem::ParamType::eFloat, 0.35f),
            .hasRange     = true,
            .range        = {.min = 0.05, .max = 1.0},
        });
        desc.params.push_back(vshadersystem::MaterialParamDesc {
            .name         = "tint",
            .type         = vshadersystem::ParamType::eVec4,
            .offset       = 16,
            .size         = 16,
            .semantic     = vshadersystem::Semantic::eBaseColor,
            .hasDefault   = true,
            .defaultValue = shaderDefault(vshadersystem::ParamType::eVec4, glm::vec4 {1.0f, 0.5f, 0.25f, 1.0f}),
        });
        desc.params.push_back(vshadersystem::MaterialParamDesc {
            .name = "unsupportedMatrix",
            .type = vshadersystem::ParamType::eMat4,
        });
        desc.textures.push_back(vshadersystem::MaterialTextureDesc {
            .name     = "albedo",
            .type     = vshadersystem::TextureType::eTex2D,
            .set      = 3,
            .binding  = 0,
            .semantic = vshadersystem::Semantic::eBaseColor,
        });

        const auto schema = materialSourceSchemaFromShaderMaterialDescription(desc);
        require(schema.parameters.size() == 3, "shader material schema should include supported params and textures");
        require(schema.parameters[0].name == "roughness", "shader float param name should convert");
        require(schema.parameters[0].type == MaterialPropertyType::eFloat, "shader float param type should convert");
        require(std::get<float>(schema.parameters[0].defaultValue) == 0.35f,
                "shader float default should convert");
        require(schema.parameters[0].hasUiRange && schema.parameters[0].uiMin == 0.05f &&
                    schema.parameters[0].uiMax == 1.0f,
                "shader param range should convert");
        require(schema.parameters[1].type == MaterialPropertyType::eVec4, "shader vec4 param type should convert");
        require(std::holds_alternative<glm::vec4>(schema.parameters[1].defaultValue),
                "shader vec4 default should convert");
        require(schema.parameters[2].type == MaterialPropertyType::eTexture2D,
                "shader texture2D should convert to texture material property");
    }

    {
        constexpr std::string_view shaderText = R"shader(
[vshader]
language = glsl
version = 460

[properties]
tint      : vec4 = (1, 0.5, 0.25, 1)
roughness : float = 0.35
albedoTex : Texture2D

[frag]
#include "vultra/mesh_material.glsl"

VULTRA_MATERIAL_MAIN(shade)

void shade(in VultraMaterialInput IN, inout VultraMaterialEval OUT)
{
    Material m = VULTRA_MATERIAL();
    OUT.baseColor = m.tint * VULTRA_SAMPLE2D(m.albedoTex_index, IN.uv0);
    OUT.roughness = m.roughness;
}
)shader";

        vshadersystem::BuildRequest request;
        request.source.virtualPath = "tests/material_asset/schema_probe.frag.vshader";
        request.source.sourceText  = std::string(shaderText);
        request.options.stage      = vshadersystem::ShaderStage::eFrag;
        request.options.materialAccessMode = vshadersystem::MaterialAccessMode::eSSBO;
        request.options.materialInjection = vshadersystem::CompileOptions::MaterialAccessInjection {
            .postMaterialDecl =
                "layout(set = 1, binding = 1, std430) readonly buffer VultraMaterialBlock\n"
                "{\n"
                "    Material vshader_Material;\n"
                "};\n"
                "Material vshader_LoadMaterial() { return vshader_Material; }\n",
            .bindlessTextureArrayName = "u_BindlessTextures",
            .macroPrefix              = "VULTRA_",
        };
        const auto repoRoot = findRepoRoot();
        require(!repoRoot.empty(), "repo root with mesh material ABI include should be discoverable");
        const auto includeRoot = repoRoot / "builtin/shaders/include";
        const auto meshMaterialInclude = readTextFile(includeRoot / "vultra/mesh_material.glsl");
        const auto colorInclude = readTextFile(includeRoot / "common/color.glsl");
        const auto mathInclude = readTextFile(includeRoot / "common/math.glsl");
        require(!meshMaterialInclude.empty(), "mesh material ABI include should be readable");
        require(!colorInclude.empty(), "color include should be readable");
        require(!mathInclude.empty(), "math include should be readable");
        request.options.virtualIncludeFiles.push_back({
            .virtualPath = "vultra/mesh_material.glsl",
            .sourceText  = meshMaterialInclude,
        });
        request.options.virtualIncludeFiles.push_back({
            .virtualPath = "common/color.glsl",
            .sourceText  = colorInclude,
        });
        request.options.virtualIncludeFiles.push_back({
            .virtualPath = "common/math.glsl",
            .sourceText  = mathInclude,
        });
        request.enableCache        = false;

        auto result = vshadersystem::build_single_shader(request);
        require(result.isOk(), result.isOk() ? "real vshader should compile" : result.error().message);

        auto webgpuRequest = request;
        webgpuRequest.options.webgpuProfile = true;
        auto webgpuResult = vshadersystem::build_single_shader(webgpuRequest);
        require(webgpuResult.isOk(),
                webgpuResult.isOk() ? "real vshader should compile for WebGPU profile" :
                                      webgpuResult.error().message);

        const auto schema =
            materialSourceSchemaFromShaderMaterialDescription(result.value().binary.materialDesc);
        require(schema.parameters.size() == 3, "real vshader material schema should expose params and textures");

        const auto findParam = [&](std::string_view name) -> const MaterialPropertySchema* {
            for (const auto& param : schema.parameters)
                if (param.name == name)
                    return &param;
            return nullptr;
        };

        const auto* tint = findParam("tint");
        require(tint && tint->type == MaterialPropertyType::eVec4, "real vshader vec4 property should reflect");
        require(tint && std::holds_alternative<glm::vec4>(tint->defaultValue),
                "real vshader vec4 default should reflect");

        const auto* roughness = findParam("roughness");
        require(roughness && roughness->type == MaterialPropertyType::eFloat,
                "real vshader float property should reflect");
        require(roughness && std::get<float>(roughness->defaultValue) == 0.35f,
                "real vshader float default should reflect");

        const auto* albedoTex = findParam("albedoTex");
        require(albedoTex && albedoTex->type == MaterialPropertyType::eTexture2D,
                "real vshader Texture2D property should reflect");

        constexpr std::string_view constantOnlyShaderText = R"shader(
[vshader]
language = glsl
version = 460

[frag]
#include "vultra/mesh_material.glsl"

VULTRA_MATERIAL_MAIN(shade)

void shade(in VultraMaterialInput IN, inout VultraMaterialEval OUT)
{
    float pulse = 0.5 + 0.5 * sin(VULTRA_TIME + IN.positionWS.x);
    OUT.baseColor = vec4(mix(vec3(0.1, 0.3, 0.8), vec3(1.0, 0.2, 0.05), pulse), 1.0);
    OUT.roughness = 0.6;
}
)shader";

        vshadersystem::BuildRequest constantOnlyRequest;
        constantOnlyRequest.source.virtualPath = "tests/material_asset/constant_only.frag.vshader";
        constantOnlyRequest.source.sourceText  = std::string(constantOnlyShaderText);
        constantOnlyRequest.options.stage      = vshadersystem::ShaderStage::eFrag;
        constantOnlyRequest.options.virtualIncludeFiles = request.options.virtualIncludeFiles;
        constantOnlyRequest.enableCache = false;
        auto constantOnlyResult = vshadersystem::build_single_shader(constantOnlyRequest);
        require(constantOnlyResult.isOk(),
                constantOnlyResult.isOk() ? "constant-only mesh material shader should compile" :
                                            constantOnlyResult.error().message);
        auto constantOnlyWebgpuRequest = constantOnlyRequest;
        constantOnlyWebgpuRequest.options.webgpuProfile = true;
        auto constantOnlyWebgpuResult = vshadersystem::build_single_shader(constantOnlyWebgpuRequest);
        require(constantOnlyWebgpuResult.isOk(),
                constantOnlyWebgpuResult.isOk() ?
                    "constant-only mesh material shader should compile for WebGPU profile" :
                    constantOnlyWebgpuResult.error().message);
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
