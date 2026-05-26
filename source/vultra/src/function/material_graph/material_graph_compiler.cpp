#include "vultra/function/material_graph/material_graph_compiler.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_map>

namespace vultra::material_graph
{
    namespace
    {
        bool hasErrors(const std::vector<Diagnostic>& diagnostics)
        {
            return std::ranges::any_of(diagnostics, [](const Diagnostic& diagnostic) {
                return diagnostic.severity == Diagnostic::Severity::eError;
            });
        }

        std::string scalarLiteral(const nlohmann::json& value, const float fallback = 0.0f)
        {
            const float f = value.is_number() ? value.get<float>() : fallback;
            std::ostringstream oss;
            oss << f;
            if (oss.str().find('.') == std::string::npos)
                oss << ".0";
            return oss.str();
        }

        std::string vectorLiteral(const nlohmann::json& value, const char* typeName, const int count, const float fallback)
        {
            std::ostringstream oss;
            oss << typeName << "(";
            for (int i = 0; i < count; ++i)
            {
                if (i)
                    oss << ", ";
                if (value.is_array() && static_cast<int>(value.size()) > i && value[i].is_number())
                    oss << scalarLiteral(value[i], fallback);
                else
                    oss << scalarLiteral(fallback, fallback);
            }
            oss << ")";
            return oss.str();
        }

        std::string defaultFor(ValueType type, const std::optional<nlohmann::json>& value)
        {
            const nlohmann::json empty;
            const auto&          v = value ? *value : empty;
            switch (type)
            {
                case ValueType::eBool:
                    return v.is_boolean() && v.get<bool>() ? "true" : "false";
                case ValueType::eInt:
                    return std::to_string(v.is_number_integer() ? v.get<int>() : 0);
                case ValueType::eFloat:
                    return scalarLiteral(v, 0.0f);
                case ValueType::eVec2:
                    return vectorLiteral(v, "vec2", 2, 0.0f);
                case ValueType::eVec3:
                    return vectorLiteral(v, "vec3", 3, 0.0f);
                case ValueType::eColor:
                case ValueType::eVec4:
                    return vectorLiteral(v, "vec4", 4, 1.0f);
                case ValueType::eTexture2D:
                    if (v.is_number_unsigned())
                        return std::to_string(v.get<uint64_t>()) + "u";
                    if (v.is_number_integer() && v.get<int64_t>() >= 0)
                        return std::to_string(v.get<int64_t>()) + "u";
                    if (v.is_string())
                    {
                        const auto text = v.get<std::string>();
                        if (!text.empty() && std::ranges::all_of(text, [](const unsigned char c) { return std::isdigit(c); }))
                            return text + "u";
                    }
                    return "0u";
                case ValueType::eString:
                case ValueType::eUnknown:
                default:
                    return "";
            }
        }

        const Node* surfaceOutputNode(const Graph& graph)
        {
            for (const auto& node : graph.nodes)
                if (node.typeId == "vultra.output.surface")
                    return &node;
            return nullptr;
        }

        std::optional<Pin> descriptorInputPin(const NodeRegistry& registry, const Node& node, std::string_view pinName)
        {
            const auto* desc = registry.find(node.typeId);
            if (!desc)
                return std::nullopt;
            const auto it = std::ranges::find_if(desc->inputs, [&](const Pin& pin) { return pin.name == pinName; });
            return it == desc->inputs.end() ? std::nullopt : std::optional<Pin>(*it);
        }

        class GlslEmitter
        {
        public:
            GlslEmitter(const Graph& graph, const NodeRegistry& registry) : m_Graph(graph), m_Registry(registry) {}

            std::string inputExpr(const Node& node, std::string_view pinName)
            {
                if (const auto* link = findInputLink(m_Graph, node.id, pinName))
                    return outputExpr(link->from.nodeId, link->from.pin);

                if (node.typeId == "vultra.output.surface" && pinName == "normal")
                    return "normalWS";

                if (node.params.contains(std::string(pinName)))
                {
                    const auto pin = descriptorInputPin(m_Registry, node, pinName);
                    return defaultFor(pin ? pin->type : ValueType::eFloat, node.params.at(std::string(pinName)));
                }

                if (auto pin = descriptorInputPin(m_Registry, node, pinName))
                    return defaultFor(pin->type, pin->defaultValue);

                return "0.0";
            }

            std::string outputExpr(std::string_view nodeId, std::string_view pinName)
            {
                const std::string key = std::string(nodeId) + ":" + std::string(pinName);
                if (auto it = m_OutputCache.find(key); it != m_OutputCache.end())
                    return it->second;

                const auto* node = findNode(m_Graph, nodeId);
                if (!node)
                    return "0.0";

                const std::string expr = emitNode(*node, pinName);
                m_OutputCache[key] = expr;
                return expr;
            }

        private:
            std::string paramValue(const Node& node, std::string_view key, ValueType type, nlohmann::json fallback) const
            {
                if (node.params.contains(std::string(key)))
                    return defaultFor(type, node.params.at(std::string(key)));
                return defaultFor(type, fallback);
            }

            std::string emitNode(const Node& node, std::string_view pinName)
            {
                const auto& type = node.typeId;
                if (type == "vultra.input.uv0")
                    return "uv";
                if (type == "vultra.input.world_position")
                    return "positionWS";
                if (type == "vultra.input.world_normal")
                    return "normalWS";
                if (type == "vultra.input.view_direction")
                    return "viewDirWS";
                if (type == "vultra.input.material_index")
                    return "int(materialIndex)";

                if (type == "vultra.param.float")
                    return paramValue(node, "value", ValueType::eFloat, 0.0f);
                if (type == "vultra.param.vec2")
                    return paramValue(node, "value", ValueType::eVec2, nlohmann::json::array({0.0f, 0.0f}));
                if (type == "vultra.param.vec3")
                    return paramValue(node, "value", ValueType::eVec3, nlohmann::json::array({0.0f, 0.0f, 0.0f}));
                if (type == "vultra.param.vec4" || type == "vultra.param.color")
                    return paramValue(node, "value", ValueType::eVec4, nlohmann::json::array({1.0f, 1.0f, 1.0f, 1.0f}));
                if (type == "vultra.param.bool")
                    return paramValue(node, "value", ValueType::eBool, false);
                if (type == "vultra.param.int" || type == "vultra.param.enum")
                    return paramValue(node, "value", ValueType::eInt, 0);
                if (type == "vultra.param.texture2d")
                    return paramValue(node, "texture", ValueType::eTexture2D, 0);

                if (type == "vultra.math.add")
                    return "(" + inputExpr(node, "a") + " + " + inputExpr(node, "b") + ")";
                if (type == "vultra.math.subtract")
                    return "(" + inputExpr(node, "a") + " - " + inputExpr(node, "b") + ")";
                if (type == "vultra.math.multiply")
                    return "(" + inputExpr(node, "a") + " * " + inputExpr(node, "b") + ")";
                if (type == "vultra.math.divide")
                    return "(" + inputExpr(node, "a") + " / max(" + inputExpr(node, "b") + ", 1e-6))";
                if (type == "vultra.math.one_minus")
                    return "(1.0 - " + inputExpr(node, "v") + ")";
                if (type == "vultra.math.power")
                    return "pow(max(" + inputExpr(node, "base") + ", 0.0), " + inputExpr(node, "exponent") + ")";
                if (type == "vultra.math.min")
                    return "min(" + inputExpr(node, "a") + ", " + inputExpr(node, "b") + ")";
                if (type == "vultra.math.max")
                    return "max(" + inputExpr(node, "a") + ", " + inputExpr(node, "b") + ")";
                if (type == "vultra.math.dot")
                    return "dot(" + inputExpr(node, "a") + ", " + inputExpr(node, "b") + ")";
                if (type == "vultra.math.normalize")
                    return "normalize(" + inputExpr(node, "v") + ")";
                if (type == "vultra.math.clamp")
                    return "clamp(" + inputExpr(node, "v") + ", " + inputExpr(node, "min") + ", " + inputExpr(node, "max") + ")";
                if (type == "vultra.math.saturate")
                    return "clamp(" + inputExpr(node, "v") + ", 0.0, 1.0)";
                if (type == "vultra.math.mix")
                    return "mix(" + inputExpr(node, "a") + ", " + inputExpr(node, "b") + ", " + inputExpr(node, "t") + ")";

                if (type == "vultra.texture.sample2d")
                {
                    const auto sample = "texture(getBindlessTexture(uint(" + inputExpr(node, "texture") + ")), " + inputExpr(node, "uv") + ")";
                    if (pinName == "rgb")
                        return sample + ".rgb";
                    if (pinName == "a")
                        return sample + ".a";
                    return sample;
                }

                if (type == "vultra.utility.normal_map")
                    return "normalize(" + inputExpr(node, "normalWS") + " + (" + inputExpr(node, "sample") + " * 2.0 - 1.0))";
                if (type == "vultra.utility.fresnel")
                    return "pow(1.0 - clamp(dot(normalize(" + inputExpr(node, "normalWS") + "), normalize(" + inputExpr(node, "viewDirWS") + ")), 0.0, 1.0), " + inputExpr(node, "power") + ")";

                return pinName == "rgb" ? "vec3(0.0)" : pinName == "rgba" ? "vec4(1.0)" : "0.0";
            }

            const Graph& m_Graph;
            const NodeRegistry& m_Registry;
            std::unordered_map<std::string, std::string> m_OutputCache;
        };

        std::string alphaModeCode(const Node& output)
        {
            return std::to_string(static_cast<uint32_t>(alphaModeFromString(output.params.value("alphaMode", std::string {"Opaque"}))));
        }

        std::string shadingModelCode(const Node& output)
        {
            return std::to_string(static_cast<uint32_t>(shadingModelFromString(output.params.value("shadingModel", std::string {"PBR_MR"}))));
        }
    } // namespace

    MaterialGraphCompiler::MaterialGraphCompiler(NodeRegistry registry) : m_Registry(std::move(registry)) {}

    std::expected<CompileOutput, std::vector<Diagnostic>>
    MaterialGraphCompiler::compile(const CompileInput& input, const ICompileBackend& backend) const
    {
        return backend.compile(input, m_Registry);
    }

    std::expected<CompileOutput, std::vector<Diagnostic>>
    SurfaceFunctionBackend::compile(const CompileInput& input, const NodeRegistry& registry) const
    {
        auto diagnostics = validateGraph(input.graph, registry);
        if (input.graph.domain != Domain::eSurface)
            diagnostics.push_back({.message = "SurfaceFunctionBackend only accepts surface material graphs"});
        if (hasErrors(diagnostics))
            return std::unexpected {diagnostics};

        const auto* output = surfaceOutputNode(input.graph);
        if (!output)
            return std::unexpected {diagnostics};

        GlslEmitter emitter {input.graph, registry};
        const auto  baseColor = emitter.inputExpr(*output, "baseColor");
        const auto  normal = emitter.inputExpr(*output, "normal");
        const auto  metallic = emitter.inputExpr(*output, "metallic");
        const auto  roughness = emitter.inputExpr(*output, "roughness");
        const auto  ao = emitter.inputExpr(*output, "ao");
        const auto  emissive = emitter.inputExpr(*output, "emissive");
        const auto  alpha = emitter.inputExpr(*output, "alpha");
        const auto  alphaCutoff = emitter.inputExpr(*output, "alphaCutoff");

        std::ostringstream src;
        const auto graphSymbol = sanitizeShaderId(input.shaderId);
        src << "// Generated by Vultra material graph compiler. Include from a graph-aware surface shader.\n";
        src << "#ifndef VULTRA_MATERIAL_GRAPH_SURFACE_DECLARED\n";
        src << "#define VULTRA_MATERIAL_GRAPH_SURFACE_DECLARED\n";
        src << "struct MaterialGraphSurface\n{\n";
        src << "    vec4 baseColor;\n";
        src << "    vec3 normalWS;\n";
        src << "    float metallic;\n";
        src << "    float roughness;\n";
        src << "    float ao;\n";
        src << "    vec3 emissive;\n";
        src << "    float alpha;\n";
        src << "    float alphaCutoff;\n";
        src << "    uint alphaMode;\n";
        src << "    uint shadingModel;\n";
        src << "};\n";
        src << "#endif\n\n";
        src << "const uint VULTRA_MATERIAL_GRAPH_ID_" << graphSymbol << " = " << input.graphId << "u;\n\n";
        src << "MaterialGraphSurface eval_material_graph_" << graphSymbol
            << "(uint materialIndex, vec2 uv, vec3 positionWS, vec3 normalWS, vec3 viewDirWS)\n{\n";
        src << "    MaterialGraphSurface surface;\n";
        src << "    surface.baseColor = " << baseColor << ";\n";
        src << "    surface.normalWS = normalize(" << normal << ");\n";
        src << "    surface.metallic = clamp(" << metallic << ", 0.0, 1.0);\n";
        src << "    surface.roughness = clamp(" << roughness << ", 0.045, 1.0);\n";
        src << "    surface.ao = clamp(" << ao << ", 0.0, 1.0);\n";
        src << "    surface.emissive = " << emissive << ";\n";
        src << "    surface.alpha = clamp(" << alpha << ", 0.0, 1.0);\n";
        src << "    surface.alphaCutoff = clamp(" << alphaCutoff << ", 0.0, 1.0);\n";
        src << "    surface.alphaMode = " << alphaModeCode(*output) << "u;\n";
        src << "    surface.shadingModel = " << shadingModelCode(*output) << "u;\n";
        src << "    if (surface.shadingModel == 3u)\n";
        src << "    {\n";
        src << "        surface.metallic = 0.0;\n";
        src << "    }\n";
        src << "    if (surface.shadingModel == 4u)\n";
        src << "    {\n";
        src << "        surface.metallic = 0.0;\n";
        src << "        surface.roughness = clamp(surface.roughness, 0.25, 1.0);\n";
        src << "    }\n";
        src << "    if (surface.shadingModel == 1u)\n";
        src << "    {\n";
        src << "        surface.metallic = 0.0;\n";
        src << "        surface.roughness = 1.0;\n";
        src << "        surface.emissive += surface.baseColor.rgb;\n";
        src << "    }\n";
        src << "    return surface;\n";
        src << "}\n";

        return CompileOutput {
            .shaderId = input.shaderId,
            .vshaderSource = src.str(),
            .diagnostics = std::move(diagnostics),
        };
    }

    uint32_t stableGraphId(const std::string_view text)
    {
        uint32_t hash = 2166136261u;
        for (const char c : text)
        {
            hash ^= static_cast<uint8_t>(c);
            hash *= 16777619u;
        }
        return hash == 0u ? 1u : hash;
    }

    std::string sanitizeShaderId(const std::string_view value)
    {
        std::string out;
        out.reserve(value.size());
        for (const char c : value)
        {
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_')
                out.push_back(c);
            else
                out.push_back('_');
        }
        if (out.empty())
            out = "material_graph";
        return out;
    }
} // namespace vultra::material_graph
