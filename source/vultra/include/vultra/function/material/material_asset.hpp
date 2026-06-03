#pragma once

#include "vultra/function/material_graph/material_graph.hpp"

#include <vshadersystem/types.hpp>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <nlohmann/json.hpp>

#include <cstring>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace vultra::material
{
    enum class MaterialSourceKind : uint8_t
    {
        eBuiltin,
        eShader,
        eGraph,
    };

    enum class MaterialPropertyType : uint8_t
    {
        eFloat,
        eInt,
        eBool,
        eVec2,
        eVec3,
        eVec4,
        eColor,
        eTexture2D,
    };

    using MaterialPropertyValue =
        std::variant<float, int32_t, bool, glm::vec2, glm::vec3, glm::vec4, std::string>;

    struct MaterialSourceRef
    {
        MaterialSourceKind kind {MaterialSourceKind::eBuiltin};
        std::string        id;
        std::string        uri;
        std::string        shaderLibrary;
    };

    struct MaterialPropertySchema
    {
        std::string           name;
        MaterialPropertyType  type {MaterialPropertyType::eFloat};
        MaterialPropertyValue defaultValue {0.0f};
        std::string           displayName;
        float                 uiMin {0.0f};
        float                 uiMax {1.0f};
        bool                  hasUiRange {false};
    };

    struct MaterialSourceSchema
    {
        std::vector<MaterialPropertySchema> parameters;
    };

    struct MaterialAsset
    {
        uint32_t version {1};
        std::string name;
        MaterialSourceRef source;
        std::vector<MaterialPropertySchema> schema;
        std::unordered_map<std::string, MaterialPropertyValue> properties;
    };

    struct ResolvedMaterial
    {
        MaterialSourceRef source;
        std::unordered_map<std::string, MaterialPropertyValue> properties;
    };

    struct MaterialAssetParseResult
    {
        MaterialAsset asset;
        std::vector<std::string> diagnostics;

        [[nodiscard]] bool ok() const { return diagnostics.empty(); }
    };

    inline MaterialSourceKind materialSourceKindFromString(const std::string_view text)
    {
        if (text == "shader")
            return MaterialSourceKind::eShader;
        if (text == "graph")
            return MaterialSourceKind::eGraph;
        return MaterialSourceKind::eBuiltin;
    }

    inline const char* materialSourceKindToString(const MaterialSourceKind kind)
    {
        switch (kind)
        {
            case MaterialSourceKind::eShader:
                return "shader";
            case MaterialSourceKind::eGraph:
                return "graph";
            case MaterialSourceKind::eBuiltin:
            default:
                return "builtin";
        }
    }

    inline std::optional<MaterialSourceKind> materialSourceKindFromStringStrict(const std::string_view text)
    {
        if (text == "builtin")
            return MaterialSourceKind::eBuiltin;
        if (text == "shader")
            return MaterialSourceKind::eShader;
        if (text == "graph")
            return MaterialSourceKind::eGraph;
        return std::nullopt;
    }

    inline std::optional<glm::vec2> materialJsonVec2(const nlohmann::json& value)
    {
        if (!value.is_array() || value.size() != 2)
            return std::nullopt;
        if (!value[0].is_number() || !value[1].is_number())
            return std::nullopt;
        return glm::vec2 {value[0].get<float>(), value[1].get<float>()};
    }

    inline std::optional<glm::vec3> materialJsonVec3(const nlohmann::json& value)
    {
        if (!value.is_array() || value.size() != 3)
            return std::nullopt;
        if (!value[0].is_number() || !value[1].is_number() || !value[2].is_number())
            return std::nullopt;
        return glm::vec3 {value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
    }

    inline std::optional<glm::vec4> materialJsonVec4(const nlohmann::json& value)
    {
        if (!value.is_array() || value.size() != 4)
            return std::nullopt;
        if (!value[0].is_number() || !value[1].is_number() || !value[2].is_number() || !value[3].is_number())
            return std::nullopt;
        return glm::vec4 {
            value[0].get<float>(), value[1].get<float>(), value[2].get<float>(), value[3].get<float>()};
    }

    inline std::optional<MaterialPropertyValue> materialPropertyValueFromJson(const nlohmann::json& value)
    {
        if (value.is_number_float())
            return value.get<float>();
        if (value.is_number_integer())
            return value.get<int32_t>();
        if (value.is_boolean())
            return value.get<bool>();
        if (value.is_string())
            return value.get<std::string>();
        if (auto vec2 = materialJsonVec2(value))
            return *vec2;
        if (auto vec3 = materialJsonVec3(value))
            return *vec3;
        if (auto vec4 = materialJsonVec4(value))
            return *vec4;
        return std::nullopt;
    }

    inline MaterialAssetParseResult materialAssetFromJson(const nlohmann::json& root)
    {
        MaterialAssetParseResult result;
        auto&                    asset = result.asset;

        if (!root.is_object())
        {
            result.diagnostics.push_back("Material root must be an object.");
            return result;
        }

        const auto type = root.value("type", std::string {"Material"});
        if (type != "Material")
            result.diagnostics.push_back("Material type must be \"Material\".");

        if (root.contains("version"))
        {
            if (!root["version"].is_number_unsigned() && !root["version"].is_number_integer())
            {
                result.diagnostics.push_back("Material version must be an integer.");
            }
            else
            {
                const auto version = root["version"].get<int64_t>();
                if (version <= 0)
                    result.diagnostics.push_back("Material version must be positive.");
                else
                    asset.version = static_cast<uint32_t>(version);
            }
        }

        asset.name = root.value("name", std::string {});

        const auto* sourceJson = root.contains("source") && root["source"].is_object() ? &root["source"] : nullptr;
        if (!sourceJson)
        {
            result.diagnostics.push_back("Material source must be an object.");
        }
        else
        {
            bool       hasValidSourceKind = false;
            const auto kindText = sourceJson->value("kind", std::string {});
            if (kindText.empty())
            {
                result.diagnostics.push_back("Material source.kind is required.");
            }
            else if (auto kind = materialSourceKindFromStringStrict(kindText))
            {
                asset.source.kind = *kind;
                hasValidSourceKind = true;
            }
            else
            {
                result.diagnostics.push_back("Unknown material source.kind: " + kindText + ".");
            }

            asset.source.id            = sourceJson->value("id", std::string {});
            asset.source.uri           = sourceJson->value("uri", std::string {});
            asset.source.shaderLibrary = sourceJson->value("shaderLibrary", std::string {});

            if (hasValidSourceKind)
            {
                switch (asset.source.kind)
                {
                    case MaterialSourceKind::eBuiltin:
                        if (asset.source.id.empty())
                            result.diagnostics.push_back("Builtin material source requires id.");
                        break;
                    case MaterialSourceKind::eGraph:
                        if (asset.source.uri.empty())
                            result.diagnostics.push_back("Graph material source requires uri.");
                        break;
                    case MaterialSourceKind::eShader:
                        if (asset.source.id.empty())
                            result.diagnostics.push_back("Shader material source requires id.");
                        if (asset.source.shaderLibrary.empty())
                            asset.source.shaderLibrary = "project";
                        break;
                }
            }
        }

        if (root.contains("properties"))
        {
            if (!root["properties"].is_object())
            {
                result.diagnostics.push_back("Material properties must be an object.");
            }
            else
            {
                for (const auto& [name, value] : root["properties"].items())
                {
                    if (auto property = materialPropertyValueFromJson(value))
                        asset.properties[name] = std::move(*property);
                    else
                        result.diagnostics.push_back("Unsupported material property value for '" + name + "'.");
                }
            }
        }

        return result;
    }

    inline MaterialAssetParseResult loadMaterialAssetFromText(const std::string_view text)
    {
        try
        {
            return materialAssetFromJson(nlohmann::json::parse(text));
        }
        catch (const nlohmann::json::exception& e)
        {
            MaterialAssetParseResult result;
            result.diagnostics.push_back(std::string {"Failed to parse material JSON: "} + e.what());
            return result;
        }
    }

    inline MaterialPropertySchema materialFloatParam(std::string name,
                                                     std::string displayName,
                                                     const float defaultValue,
                                                     const float uiMin,
                                                     const float uiMax)
    {
        return MaterialPropertySchema {
            .name         = std::move(name),
            .type         = MaterialPropertyType::eFloat,
            .defaultValue = defaultValue,
            .displayName  = std::move(displayName),
            .uiMin        = uiMin,
            .uiMax        = uiMax,
            .hasUiRange   = true,
        };
    }

    inline MaterialPropertySchema materialBoolParam(std::string name, std::string displayName, const bool defaultValue)
    {
        return MaterialPropertySchema {
            .name         = std::move(name),
            .type         = MaterialPropertyType::eBool,
            .defaultValue = defaultValue,
            .displayName  = std::move(displayName),
        };
    }

    inline MaterialPropertySchema materialColorParam(std::string name,
                                                     std::string displayName,
                                                     const glm::vec4& defaultValue)
    {
        return MaterialPropertySchema {
            .name         = std::move(name),
            .type         = MaterialPropertyType::eColor,
            .defaultValue = defaultValue,
            .displayName  = std::move(displayName),
            .uiMin        = 0.0f,
            .uiMax        = 1.0f,
            .hasUiRange   = true,
        };
    }

    inline MaterialPropertySchema materialTextureParam(std::string name, std::string displayName)
    {
        return MaterialPropertySchema {
            .name         = std::move(name),
            .type         = MaterialPropertyType::eTexture2D,
            .defaultValue = std::string {},
            .displayName  = std::move(displayName),
        };
    }

    inline MaterialSourceSchema builtinPbrMaterialSchema()
    {
        return MaterialSourceSchema {
            .parameters {
                materialColorParam("baseColor", "Base Color", glm::vec4 {1.0f, 1.0f, 1.0f, 1.0f}),
                materialFloatParam("metallic", "Metallic", 0.0f, 0.0f, 1.0f),
                materialFloatParam("roughness", "Roughness", 0.5f, 0.045f, 1.0f),
                materialFloatParam("alphaCutoff", "Alpha Cutoff", 0.5f, 0.0f, 1.0f),
                materialBoolParam("doubleSided", "Double Sided", false),
                materialTextureParam("baseColorTexture", "Base Color Texture"),
                materialTextureParam("normalTexture", "Normal Texture"),
                materialTextureParam("metallicTexture", "Metallic Texture"),
                materialTextureParam("roughnessTexture", "Roughness Texture"),
                materialTextureParam("metallicRoughnessTexture", "Metallic Roughness Texture"),
                materialTextureParam("ambientOcclusionTexture", "Ambient Occlusion Texture"),
                materialTextureParam("emissiveTexture", "Emissive Texture"),
            },
        };
    }

    inline MaterialSourceSchema resolveMaterialSourceSchema(const MaterialSourceRef& source)
    {
        if (source.kind == MaterialSourceKind::eBuiltin && source.id == "builtin/pbr")
            return builtinPbrMaterialSchema();

        // Graph blackboards are resolved from loaded graph data by
        // materialSourceSchemaFromGraph(). Shader sources are resolved from
        // vshadersystem::MaterialDescription after loading a compiled shader.
        return {};
    }

    inline MaterialPropertyType materialPropertyTypeFromShaderParamType(const vshadersystem::ParamType type)
    {
        switch (type)
        {
            case vshadersystem::ParamType::eVec2:
                return MaterialPropertyType::eVec2;
            case vshadersystem::ParamType::eVec3:
                return MaterialPropertyType::eVec3;
            case vshadersystem::ParamType::eVec4:
                return MaterialPropertyType::eVec4;
            case vshadersystem::ParamType::eInt:
            case vshadersystem::ParamType::eUInt:
                return MaterialPropertyType::eInt;
            case vshadersystem::ParamType::eBool:
                return MaterialPropertyType::eBool;
            case vshadersystem::ParamType::eFloat:
            default:
                return MaterialPropertyType::eFloat;
        }
    }

    inline bool shaderParamTypeIsMaterialSchemaSupported(const vshadersystem::ParamType type)
    {
        switch (type)
        {
            case vshadersystem::ParamType::eFloat:
            case vshadersystem::ParamType::eVec2:
            case vshadersystem::ParamType::eVec3:
            case vshadersystem::ParamType::eVec4:
            case vshadersystem::ParamType::eInt:
            case vshadersystem::ParamType::eUInt:
            case vshadersystem::ParamType::eBool:
                return true;
            case vshadersystem::ParamType::eMat3:
            case vshadersystem::ParamType::eMat4:
            default:
                return false;
        }
    }

    template<typename T>
    inline T shaderParamDefaultAs(const vshadersystem::ParamDefault& value, const T fallback)
    {
        T out = fallback;
        std::memcpy(&out, value.valueBuffer, sizeof(T));
        return out;
    }

    inline MaterialPropertyValue materialPropertyDefaultFromShaderParam(const vshadersystem::MaterialParamDesc& param)
    {
        if (!param.hasDefault)
        {
            switch (param.type)
            {
                case vshadersystem::ParamType::eVec2:
                    return glm::vec2 {0.0f};
                case vshadersystem::ParamType::eVec3:
                    return glm::vec3 {0.0f};
                case vshadersystem::ParamType::eVec4:
                    return glm::vec4 {0.0f};
                case vshadersystem::ParamType::eInt:
                case vshadersystem::ParamType::eUInt:
                    return int32_t {0};
                case vshadersystem::ParamType::eBool:
                    return false;
                case vshadersystem::ParamType::eFloat:
                default:
                    return 0.0f;
            }
        }

        switch (param.type)
        {
            case vshadersystem::ParamType::eVec2:
                return shaderParamDefaultAs(param.defaultValue, glm::vec2 {0.0f});
            case vshadersystem::ParamType::eVec3:
                return shaderParamDefaultAs(param.defaultValue, glm::vec3 {0.0f});
            case vshadersystem::ParamType::eVec4:
                return shaderParamDefaultAs(param.defaultValue, glm::vec4 {0.0f});
            case vshadersystem::ParamType::eInt:
                return shaderParamDefaultAs(param.defaultValue, int32_t {0});
            case vshadersystem::ParamType::eUInt:
                return static_cast<int32_t>(shaderParamDefaultAs(param.defaultValue, uint32_t {0}));
            case vshadersystem::ParamType::eBool:
                return shaderParamDefaultAs(param.defaultValue, bool {false});
            case vshadersystem::ParamType::eFloat:
            default:
                return shaderParamDefaultAs(param.defaultValue, 0.0f);
        }
    }

    inline std::optional<std::string>
    shaderTexturePropertyNameFromIndexParam(const vshadersystem::MaterialParamDesc& param)
    {
        if ((param.type != vshadersystem::ParamType::eInt && param.type != vshadersystem::ParamType::eUInt) ||
            !param.name.ends_with("_index"))
        {
            return std::nullopt;
        }

        auto name = param.name.substr(0, param.name.size() - std::string_view("_index").size());
        return name.empty() ? std::nullopt : std::optional<std::string> {std::move(name)};
    }

    inline MaterialSourceSchema
    materialSourceSchemaFromShaderMaterialDescription(const vshadersystem::MaterialDescription& desc)
    {
        MaterialSourceSchema schema;
        schema.parameters.reserve(desc.params.size() + desc.textures.size());

        for (const auto& param : desc.params)
        {
            if (param.name.empty() || !shaderParamTypeIsMaterialSchemaSupported(param.type))
                continue;

            if (auto textureName = shaderTexturePropertyNameFromIndexParam(param))
            {
                auto name = std::move(*textureName);
                schema.parameters.push_back(MaterialPropertySchema {
                    .name         = name,
                    .type         = MaterialPropertyType::eTexture2D,
                    .defaultValue = std::string {},
                    .displayName  = std::move(name),
                });
                continue;
            }

            schema.parameters.push_back(MaterialPropertySchema {
                .name         = param.name,
                .type         = materialPropertyTypeFromShaderParamType(param.type),
                .defaultValue = materialPropertyDefaultFromShaderParam(param),
                .displayName  = param.name,
                .uiMin        = static_cast<float>(param.range.min),
                .uiMax        = static_cast<float>(param.range.max),
                .hasUiRange   = param.hasRange,
            });
        }

        for (const auto& texture : desc.textures)
        {
            if (texture.name.empty() || texture.type != vshadersystem::TextureType::eTex2D)
                continue;

            schema.parameters.push_back(MaterialPropertySchema {
                .name         = texture.name,
                .type         = MaterialPropertyType::eTexture2D,
                .defaultValue = std::string {},
                .displayName  = texture.name,
            });
        }

        return schema;
    }

    inline MaterialSourceSchema materialSourceSchemaFromShaderReflection(const MaterialSourceRef& source)
    {
        if (source.kind != MaterialSourceKind::eShader || source.id.empty())
            return {};

        // A live shader library is needed to load vshadersystem::MaterialDescription.
        // Call materialSourceSchemaFromShaderMaterialDescription() after resolving
        // the shader source to a compiled library variant.
        return {};
    }

    inline MaterialPropertyType materialPropertyTypeFromGraphValueType(const material_graph::ValueType type)
    {
        switch (type)
        {
            case material_graph::ValueType::eBool:
                return MaterialPropertyType::eBool;
            case material_graph::ValueType::eInt:
                return MaterialPropertyType::eInt;
            case material_graph::ValueType::eVec2:
                return MaterialPropertyType::eVec2;
            case material_graph::ValueType::eVec3:
                return MaterialPropertyType::eVec3;
            case material_graph::ValueType::eVec4:
                return MaterialPropertyType::eVec4;
            case material_graph::ValueType::eColor:
                return MaterialPropertyType::eColor;
            case material_graph::ValueType::eTexture2D:
                return MaterialPropertyType::eTexture2D;
            case material_graph::ValueType::eFloat:
            default:
                return MaterialPropertyType::eFloat;
        }
    }

    inline MaterialPropertyValue materialPropertyDefaultFromGraph(const material_graph::ValueType type,
                                                                  const nlohmann::json&          value)
    {
        switch (type)
        {
            case material_graph::ValueType::eBool:
                return value.is_boolean() ? value.get<bool>() : false;
            case material_graph::ValueType::eInt:
                return value.is_number_integer() ? value.get<int32_t>() : int32_t {0};
            case material_graph::ValueType::eVec2:
                if (value.is_array() && value.size() >= 2 && value[0].is_number() && value[1].is_number())
                    return glm::vec2 {value[0].get<float>(), value[1].get<float>()};
                return glm::vec2 {0.0f};
            case material_graph::ValueType::eVec3:
                if (value.is_array() && value.size() >= 3 && value[0].is_number() && value[1].is_number() &&
                    value[2].is_number())
                {
                    return glm::vec3 {value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
                }
                return glm::vec3 {0.0f};
            case material_graph::ValueType::eVec4:
            case material_graph::ValueType::eColor:
                if (value.is_array() && value.size() >= 4 && value[0].is_number() && value[1].is_number() &&
                    value[2].is_number() && value[3].is_number())
                {
                    return glm::vec4 {
                        value[0].get<float>(), value[1].get<float>(), value[2].get<float>(), value[3].get<float>()};
                }
                return type == material_graph::ValueType::eColor ? glm::vec4 {1.0f} : glm::vec4 {0.0f};
            case material_graph::ValueType::eTexture2D:
            case material_graph::ValueType::eString:
                return value.is_string() ? value.get<std::string>() : std::string {};
            case material_graph::ValueType::eFloat:
            default:
                return value.is_number() ? value.get<float>() : 0.0f;
        }
    }

    inline MaterialSourceSchema materialSourceSchemaFromGraph(const material_graph::Graph& graph)
    {
        MaterialSourceSchema schema;
        schema.parameters.reserve(graph.blackboard.size());
        for (const auto& param : graph.blackboard)
        {
            schema.parameters.push_back(MaterialPropertySchema {
                .name         = param.name,
                .type         = materialPropertyTypeFromGraphValueType(param.type),
                .defaultValue = materialPropertyDefaultFromGraph(param.type, param.defaultValue),
                .displayName  = param.displayName.empty() ? param.name : param.displayName,
                .uiMin        = param.uiMin,
                .uiMax        = param.uiMax,
                .hasUiRange   = param.hasUiRange,
            });
        }
        return schema;
    }
} // namespace vultra::material
