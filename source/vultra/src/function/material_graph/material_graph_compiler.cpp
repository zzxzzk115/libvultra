#include "vultra/function/material_graph/material_graph_compiler.hpp"

#include "vultra/core/base/common_context.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
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
            const float        f = value.is_number() ? value.get<float>() : fallback;
            std::ostringstream oss;
            oss << f;
            if (oss.str().find('.') == std::string::npos)
                oss << ".0";
            return oss.str();
        }

        std::string
        vectorLiteral(const nlohmann::json& value, const char* typeName, const int count, const float fallback)
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
                        if (!text.empty() &&
                            std::ranges::all_of(text, [](const unsigned char c) { return std::isdigit(c); }))
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
                if (isSurfaceOutputType(node.typeId))
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

        std::string replaceAll(std::string text, std::string_view from, std::string_view to)
        {
            if (from.empty())
                return text;
            size_t pos = 0;
            while ((pos = text.find(from, pos)) != std::string::npos)
            {
                text.replace(pos, from.size(), to);
                pos += to.size();
            }
            return text;
        }

        class GlslEmitter
        {
        public:
            GlslEmitter(const Graph& graph, const NodeRegistry& registry) : m_Graph(graph), m_Registry(registry) {}

            std::string inputExpr(const Node& node, std::string_view pinName)
            {
                if (const auto* link = findInputLink(m_Graph, node.id, pinName))
                    return outputExpr(link->from.nodeId, link->from.pin);

                if (isSurfaceOutputType(node.typeId) && pinName == "normal")
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
                m_OutputCache[key]     = expr;
                return expr;
            }

        private:
            std::string
            paramValue(const Node& node, std::string_view key, ValueType type, nlohmann::json fallback) const
            {
                if (node.params.contains(std::string(key)))
                    return defaultFor(type, node.params.at(std::string(key)));
                return defaultFor(type, fallback);
            }

            std::string customSnippetExpr(const Node& node,
                                          const NodeDescriptor& desc,
                                          std::string_view pinName)
            {
                if (!desc.implementation.is_object() ||
                    desc.implementation.value("language", std::string {"glsl"}) != "glsl")
                {
                    return {};
                }

                // Collect helper/BXDF includes this node references (by name) so the
                // emitted surface source pulls them in and can call their functions.
                if (auto incIt = desc.implementation.find("includes");
                    incIt != desc.implementation.end() && incIt->is_array())
                {
                    for (const auto& inc : *incIt)
                    {
                        if (!inc.is_string())
                            continue;
                        const auto path = inc.get<std::string>();
                        if (!path.empty() &&
                            std::find(m_Includes.begin(), m_Includes.end(), path) == m_Includes.end())
                            m_Includes.push_back(path);
                    }
                }

                const auto* outputs =
                    desc.implementation.contains("outputs") && desc.implementation["outputs"].is_object() ?
                        &desc.implementation["outputs"] :
                        nullptr;
                if (!outputs)
                    return {};

                const auto it = outputs->find(std::string(pinName));
                if (it == outputs->end() || !it->is_string())
                    return {};

                std::string expr = it->get<std::string>();
                for (const auto& input : desc.inputs)
                    expr = replaceAll(expr, "{{input:" + input.name + "}}", inputExpr(node, input.name));

                for (const auto& [key, value] : desc.defaultParams.items())
                {
                    const auto pin  = descriptorInputPin(m_Registry, node, key);
                    const auto type = pin ? pin->type : ValueType::eFloat;
                    expr = replaceAll(expr,
                                      "{{param:" + key + "}}",
                                      node.params.contains(key) ? defaultFor(type, node.params.at(key)) :
                                                                  defaultFor(type, value));
                }
                return "(" + expr + ")";
            }

            std::string emitNode(const Node& node, std::string_view pinName)
            {
                const auto& type = node.typeId;
                if (type == "vultra.input.uv0")
                    return "ctx.uv";
                if (type == "vultra.input.world_position")
                    return "ctx.positionWS";
                if (type == "vultra.input.world_normal")
                    return "normalWS";
                if (type == "vultra.input.view_direction")
                    return "ctx.viewDirWS";
                if (type == "vultra.input.view_index")
                    return "int(ctx.viewIndex)";
                if (type == "vultra.input.eye_index")
                    return "int(ctx.eyeIndex)";
                if (type == "vultra.input.view_count")
                    return "int(ctx.viewCount)";
                if (type == "vultra.input.is_stereo_view")
                    return "ctx.isStereoView";
                if (type == "vultra.input.material_index")
                    return "int(ctx.materialIndex)";
                if (type == "vultra.input.time")
                    return "ctx.timeSeconds";

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
                if (type == "vultra.math.sine")
                    return "sin(" + inputExpr(node, "v") + ")";
                if (type == "vultra.math.fract")
                    return "fract(" + inputExpr(node, "v") + ")";
                if (type == "vultra.math.dot")
                    return "dot(" + inputExpr(node, "a") + ", " + inputExpr(node, "b") + ")";
                if (type == "vultra.math.normalize")
                    return "normalize(" + inputExpr(node, "v") + ")";
                if (type == "vultra.math.clamp")
                    return "clamp(" + inputExpr(node, "v") + ", " + inputExpr(node, "min") + ", " +
                           inputExpr(node, "max") + ")";
                if (type == "vultra.math.saturate")
                    return "clamp(" + inputExpr(node, "v") + ", 0.0, 1.0)";
                if (type == "vultra.math.smoothstep")
                    return "smoothstep(" + inputExpr(node, "edge0") + ", " + inputExpr(node, "edge1") + ", " +
                           inputExpr(node, "x") + ")";
                if (type == "vultra.math.mix")
                    return "mix(" + inputExpr(node, "a") + ", " + inputExpr(node, "b") + ", " + inputExpr(node, "t") +
                           ")";
                if (type == "vultra.vector.split_vec2")
                    return inputExpr(node, "v") + "." + (pinName == "y" ? "y" : "x");

                if (type == "vultra.texture.sample2d")
                {
                    const auto sample = "texture(getBindlessTexture(uint(" + inputExpr(node, "texture") + ")), " +
                                        inputExpr(node, "uv") + ")";
                    if (pinName == "rgb")
                        return sample + ".rgb";
                    if (pinName == "a")
                        return sample + ".a";
                    return sample;
                }

                // --- Extended unary math (float -> float) ---
                if (type == "vultra.math.abs")
                    return "abs(" + inputExpr(node, "v") + ")";
                if (type == "vultra.math.floor")
                    return "floor(" + inputExpr(node, "v") + ")";
                if (type == "vultra.math.ceil")
                    return "ceil(" + inputExpr(node, "v") + ")";
                if (type == "vultra.math.round")
                    return "floor(" + inputExpr(node, "v") + " + 0.5)";
                if (type == "vultra.math.truncate")
                    return "trunc(" + inputExpr(node, "v") + ")";
                if (type == "vultra.math.sign")
                    return "sign(" + inputExpr(node, "v") + ")";
                if (type == "vultra.math.sqrt")
                    return "sqrt(max(" + inputExpr(node, "v") + ", 0.0))";
                if (type == "vultra.math.exp")
                    return "exp(" + inputExpr(node, "v") + ")";
                if (type == "vultra.math.exp2")
                    return "exp2(" + inputExpr(node, "v") + ")";
                if (type == "vultra.math.log")
                    return "log(max(" + inputExpr(node, "v") + ", 1e-6))";
                if (type == "vultra.math.log2")
                    return "log2(max(" + inputExpr(node, "v") + ", 1e-6))";
                if (type == "vultra.math.cosine")
                    return "cos(" + inputExpr(node, "v") + ")";
                if (type == "vultra.math.tangent")
                    return "tan(" + inputExpr(node, "v") + ")";
                if (type == "vultra.math.arcsine")
                    return "asin(clamp(" + inputExpr(node, "v") + ", -1.0, 1.0))";
                if (type == "vultra.math.arccosine")
                    return "acos(clamp(" + inputExpr(node, "v") + ", -1.0, 1.0))";
                if (type == "vultra.math.arctangent")
                    return "atan(" + inputExpr(node, "v") + ")";
                if (type == "vultra.math.radians")
                    return "radians(" + inputExpr(node, "v") + ")";
                if (type == "vultra.math.degrees")
                    return "degrees(" + inputExpr(node, "v") + ")";
                if (type == "vultra.math.negate")
                    return "(-(" + inputExpr(node, "v") + "))";
                if (type == "vultra.math.reciprocal")
                    return "(1.0 / max(" + inputExpr(node, "v") + ", 1e-6))";
                if (type == "vultra.math.square")
                    return "(" + inputExpr(node, "v") + " * " + inputExpr(node, "v") + ")";

                // --- Extended binary / interp math ---
                if (type == "vultra.math.modulo")
                    return "mod(" + inputExpr(node, "a") + ", max(" + inputExpr(node, "b") + ", 1e-6))";
                if (type == "vultra.math.step")
                    return "step(" + inputExpr(node, "edge") + ", " + inputExpr(node, "x") + ")";
                if (type == "vultra.math.atan2")
                    return "atan(" + inputExpr(node, "y") + ", " + inputExpr(node, "x") + ")";
                if (type == "vultra.math.lerp")
                    return "mix(" + inputExpr(node, "a") + ", " + inputExpr(node, "b") + ", " + inputExpr(node, "t") + ")";
                if (type == "vultra.math.inverse_lerp")
                    return "((" + inputExpr(node, "v") + " - " + inputExpr(node, "a") + ") / max(" + inputExpr(node, "b") +
                           " - " + inputExpr(node, "a") + ", 1e-6))";
                if (type == "vultra.math.remap")
                    return "(" + inputExpr(node, "outMin") + " + (" + inputExpr(node, "v") + " - " +
                           inputExpr(node, "inMin") + ") * (" + inputExpr(node, "outMax") + " - " +
                           inputExpr(node, "outMin") + ") / max(" + inputExpr(node, "inMax") + " - " +
                           inputExpr(node, "inMin") + ", 1e-6))";

                // --- Vector ops ---
                if (type == "vultra.vector.cross")
                    return "cross(" + inputExpr(node, "a") + ", " + inputExpr(node, "b") + ")";
                if (type == "vultra.vector.length")
                    return "length(" + inputExpr(node, "v") + ")";
                if (type == "vultra.vector.distance")
                    return "distance(" + inputExpr(node, "a") + ", " + inputExpr(node, "b") + ")";
                if (type == "vultra.vector.reflect")
                    return "reflect(" + inputExpr(node, "i") + ", normalize(" + inputExpr(node, "n") + "))";
                if (type == "vultra.vector.scale")
                    return "(" + inputExpr(node, "v") + " * " + inputExpr(node, "scale") + ")";
                if (type == "vultra.vector.combine_vec2")
                    return "vec2(" + inputExpr(node, "x") + ", " + inputExpr(node, "y") + ")";
                if (type == "vultra.vector.combine_vec3")
                    return "vec3(" + inputExpr(node, "x") + ", " + inputExpr(node, "y") + ", " + inputExpr(node, "z") + ")";
                if (type == "vultra.vector.combine_vec4")
                    return "vec4(" + inputExpr(node, "x") + ", " + inputExpr(node, "y") + ", " + inputExpr(node, "z") +
                           ", " + inputExpr(node, "w") + ")";
                if (type == "vultra.vector.split_vec3")
                    return inputExpr(node, "v") + "." + (pinName == "y" ? "y" : pinName == "z" ? "z" : "x");
                if (type == "vultra.vector.split_vec4")
                    return inputExpr(node, "v") + "." +
                           (pinName == "y" ? "y" : pinName == "z" ? "z" : pinName == "w" ? "w" : "x");

                // --- UV manipulation ---
                if (type == "vultra.uv.tiling_offset")
                    return "(" + inputExpr(node, "uv") + " * " + inputExpr(node, "tiling") + " + " +
                           inputExpr(node, "offset") + ")";
                if (type == "vultra.uv.panner")
                    return "(" + inputExpr(node, "uv") + " + " + inputExpr(node, "speed") + " * " +
                           inputExpr(node, "time") + ")";
                if (type == "vultra.uv.rotator")
                {
                    const auto uv = inputExpr(node, "uv");
                    const auto a = inputExpr(node, "angle");
                    return "(vec2(((" + uv + ") - 0.5).x * cos(" + a + ") - ((" + uv + ") - 0.5).y * sin(" + a +
                           "), ((" + uv + ") - 0.5).x * sin(" + a + ") + ((" + uv + ") - 0.5).y * cos(" + a +
                           ")) + 0.5)";
                }

                // --- Color / procedural ---
                if (type == "vultra.color.desaturate")
                    return "mix(" + inputExpr(node, "color") + ", vec3(dot(" + inputExpr(node, "color") +
                           ", vec3(0.299, 0.587, 0.114))), " + inputExpr(node, "amount") + ")";
                if (type == "vultra.color.contrast")
                    return "((" + inputExpr(node, "color") + " - 0.5) * " + inputExpr(node, "contrast") + " + 0.5)";
                if (type == "vultra.color.posterize")
                    return "(floor(" + inputExpr(node, "color") + " * " + inputExpr(node, "steps") + ") / max(" +
                           inputExpr(node, "steps") + ", 1.0))";
                if (type == "vultra.procedural.checkerboard")
                    return "mod(floor(" + inputExpr(node, "uv") + ".x) + floor(" + inputExpr(node, "uv") + ".y), 2.0)";
                if (type == "vultra.procedural.white_noise")
                    return "fract(sin(dot(" + inputExpr(node, "uv") + ", vec2(12.9898, 78.233))) * 43758.5453)";

                if (type == "vultra.utility.normal_map")
                    return "normalize(" + inputExpr(node, "normalWS") + " + (" + inputExpr(node, "sample") +
                           " * 2.0 - 1.0))";
                if (type == "vultra.utility.fresnel")
                    return "pow(1.0 - clamp(dot(normalize(" + inputExpr(node, "normalWS") + "), normalize(" +
                           inputExpr(node, "viewDirWS") + ")), 0.0, 1.0), " + inputExpr(node, "power") + ")";

                if (const auto* desc = m_Registry.find(type))
                    if (auto expr = customSnippetExpr(node, *desc, pinName); !expr.empty())
                        return expr;

                return pinName == "rgb" ? "vec3(0.0)" : pinName == "rgba" ? "vec4(1.0)" : "0.0";
            }

        public:
            // Shader-library include paths required by the custom nodes used in the
            // graph (their implementation.includes). Emitted as #include directives
            // so a node can call a whole helper/BXDF function instead of inlining it.
            [[nodiscard]] const std::vector<std::string>& requiredIncludes() const { return m_Includes; }

        private:
            const Graph&                                 m_Graph;
            const NodeRegistry&                          m_Registry;
            std::unordered_map<std::string, std::string> m_OutputCache;
            std::vector<std::string>                     m_Includes;
        };

        std::string alphaModeCode(const Node& output)
        {
            return std::to_string(
                static_cast<uint32_t>(alphaModeFromString(output.params.value("alphaMode", std::string {"Opaque"}))));
        }

        // GBuffer / deferred-lighting model codes (must match VULTRA_MAT_* in the
        // shaders and GpuMaterialModel): PBR_MR=1, PBR_SG=2, Unlit=3, Phong=4,
        // ToonLike=6. Custom (registered) codes are >= 8 and resolved by name.
        uint32_t gbufferModelCode(const Node& output, const CompileInput& input)
        {
            const std::string_view typeId = output.typeId;
            if (typeId == "vultra.output.pbr_sg")
                return 2u;
            if (typeId == "vultra.output.unlit")
                return 3u;
            if (typeId == "vultra.output.phong")
                return 4u;
            if (typeId == "vultra.output.toon")
                return 6u;
            if (typeId == "vultra.output.custom")
            {
                const auto name = output.params.value("shadingModelName", std::string {});
                if (const auto it = input.customShadingModelCodes.find(name); it != input.customShadingModelCodes.end())
                    return it->second;
                return 1u; // unresolved custom model -> render as PBR Metallic-Roughness
            }
            return 1u; // vultra.output.pbr_mr (and any unknown output)
        }

        // Emits the GLSL that fills surface.metallic / surface.roughness / surface.ao
        // for the output node's model. The GBuffer is metallic-roughness shaped, so SG
        // and Phong are converted here exactly as the parametric/asset path does in
        // thin_gbuffer.frag (material_mra), keeping both render paths pixel-consistent.
        void emitSurfaceMra(std::ostringstream& src, const Node& output, GlslEmitter& emitter)
        {
            const std::string_view typeId = output.typeId;
            const auto             ao     = emitter.inputExpr(output, "ao");
            if (typeId == "vultra.output.pbr_mr")
            {
                src << "    surface.metallic = clamp(" << emitter.inputExpr(output, "metallic") << ", 0.0, 1.0);\n";
                src << "    surface.roughness = clamp(" << emitter.inputExpr(output, "roughness") << ", 0.045, 1.0);\n";
                src << "    surface.ao = clamp(" << ao << ", 0.0, 1.0);\n";
            }
            else if (typeId == "vultra.output.pbr_sg")
            {
                const auto specular   = emitter.inputExpr(output, "specular");
                const auto glossiness = emitter.inputExpr(output, "glossiness");
                src << "    vec3 sgSpecular = (" << specular << ").rgb;\n";
                src << "    surface.metallic = clamp(dot(sgSpecular, vec3(0.2126, 0.7152, 0.0722)), 0.0, 1.0);\n";
                src << "    surface.roughness = clamp(1.0 - (" << glossiness << "), 0.02, 1.0);\n";
                src << "    surface.ao = clamp(" << ao << ", 0.0, 1.0);\n";
            }
            else if (typeId == "vultra.output.phong")
            {
                const auto specular  = emitter.inputExpr(output, "specular");
                const auto shininess = emitter.inputExpr(output, "shininess");
                src << "    vec3 phongSpecular = (" << specular << ").rgb;\n";
                src << "    surface.metallic = clamp(dot(phongSpecular, vec3(0.2126, 0.7152, 0.0722)), 0.0, 1.0);\n";
                src << "    surface.roughness = clamp(1.0 / sqrt(max(" << shininess << ", 1.0)), 0.02, 1.0);\n";
                src << "    surface.ao = clamp(" << ao << ", 0.0, 1.0);\n";
            }
            else if (typeId == "vultra.output.toon")
            {
                // Toon lighting ignores metallic/roughness; keep dielectric-matte defaults.
                src << "    surface.metallic = 0.0;\n";
                src << "    surface.roughness = 1.0;\n";
                src << "    surface.ao = clamp(" << ao << ", 0.0, 1.0);\n";
            }
            else // unlit + custom: lighting either skipped (unlit) or handled by the BXDF (custom)
            {
                src << "    surface.metallic = 0.0;\n";
                src << "    surface.roughness = 1.0;\n";
                src << "    surface.ao = 1.0;\n";
            }
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
        // metallic/roughness/ao are emitted per-model by emitSurfaceMra (SG/Phong are
        // converted to the metallic-roughness GBuffer shape). The shared pins below
        // exist on every output node type.
        const auto  baseColor   = emitter.inputExpr(*output, "baseColor");
        const auto  normal      = emitter.inputExpr(*output, "normal");
        const auto  emissive    = emitter.inputExpr(*output, "emissive");
        const auto  alpha       = emitter.inputExpr(*output, "alpha");
        const auto  alphaCutoff = emitter.inputExpr(*output, "alphaCutoff");

        std::ostringstream src;
        const auto         graphSymbol = sanitizeShaderId(input.shaderId);
        src << "// Generated by Vultra material graph compiler. Include from a graph-aware surface shader.\n";
        for (const auto& include : emitter.requiredIncludes())
            src << "#include \"" << include << "\"\n";
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
        src << "#ifndef VULTRA_SHADER_VIEW_CONTEXT_DECLARED\n";
        src << "#define VULTRA_SHADER_VIEW_CONTEXT_DECLARED\n";
        src << "uint vultra_view_index()\n";
        src << "{\n";
        src << "#if defined(VULTRA_MULTIVIEW) && VULTRA_MULTIVIEW\n";
        src << "    return uint(gl_ViewIndex);\n";
        src << "#else\n";
        src << "    return 0u;\n";
        src << "#endif\n";
        src << "}\n";
        src << "uint vultra_view_count()\n";
        src << "{\n";
        src << "#if defined(VULTRA_VIEW_COUNT)\n";
        src << "    return uint(VULTRA_VIEW_COUNT);\n";
        src << "#elif defined(VULTRA_MULTIVIEW) && VULTRA_MULTIVIEW\n";
        src << "    return 2u;\n";
        src << "#else\n";
        src << "    return 1u;\n";
        src << "#endif\n";
        src << "}\n";
        src << "uint vultra_eye_index()\n";
        src << "{\n";
        src << "    return min(vultra_view_index(), max(vultra_view_count(), 1u) - 1u);\n";
        src << "}\n";
        src << "bool vultra_is_stereo_view()\n";
        src << "{\n";
        src << "    return vultra_view_count() > 1u;\n";
        src << "}\n";
        src << "#endif\n\n";
        src << "#ifndef VULTRA_MATERIAL_GRAPH_CONTEXT_DECLARED\n";
        src << "#define VULTRA_MATERIAL_GRAPH_CONTEXT_DECLARED\n";
        src << "struct MaterialGraphContext\n{\n";
        src << "    uint materialIndex;\n";
        src << "    vec2 uv;\n";
        src << "    vec3 positionWS;\n";
        src << "    vec3 normalWS;\n";
        src << "    vec3 viewDirWS;\n";
        src << "    float timeSeconds;\n";
        src << "    uint viewIndex;\n";
        src << "    uint eyeIndex;\n";
        src << "    uint viewCount;\n";
        src << "    bool isStereoView;\n";
        src << "};\n";
        src << "#endif\n\n";
        src << "const uint VULTRA_MATERIAL_GRAPH_ID_" << graphSymbol << " = " << input.graphId << "u;\n\n";
        src << "MaterialGraphSurface eval_material_graph_" << graphSymbol << "_ctx(MaterialGraphContext ctx)\n{\n";
        src << "    MaterialGraphSurface surface;\n";
        src << "    vec3 normalWS = ctx.normalWS;\n";
        src << "    surface.baseColor = " << baseColor << ";\n";
        src << "    surface.normalWS = normalize(" << normal << ");\n";
        emitSurfaceMra(src, *output, emitter);
        src << "    surface.emissive = " << emissive << ";\n";
        src << "    surface.alpha = clamp(" << alpha << ", 0.0, 1.0);\n";
        src << "    surface.alphaCutoff = clamp(" << alphaCutoff << ", 0.0, 1.0);\n";
        src << "    surface.alphaMode = " << alphaModeCode(*output) << "u;\n";
        // surface.shadingModel stores the GBuffer / deferred-lighting model code directly
        // (per-model output node identity), so the mesh-material backend can pass it
        // through without a runtime enum->code remap.
        src << "    surface.shadingModel = " << gbufferModelCode(*output, input) << "u;\n";
        src << "    return surface;\n";
        src << "}\n";
        src << "\n";
        src << "MaterialGraphSurface eval_material_graph_" << graphSymbol
            << "(uint materialIndex, vec2 uv, vec3 positionWS, vec3 normalWS, vec3 viewDirWS, float timeSeconds)\n{\n";
        src << "    MaterialGraphContext ctx;\n";
        src << "    ctx.materialIndex = materialIndex;\n";
        src << "    ctx.uv = uv;\n";
        src << "    ctx.positionWS = positionWS;\n";
        src << "    ctx.normalWS = normalWS;\n";
        src << "    ctx.viewDirWS = viewDirWS;\n";
        src << "    ctx.timeSeconds = timeSeconds;\n";
        src << "    ctx.viewIndex = vultra_view_index();\n";
        src << "    ctx.eyeIndex = vultra_eye_index();\n";
        src << "    ctx.viewCount = vultra_view_count();\n";
        src << "    ctx.isStereoView = vultra_is_stereo_view();\n";
        src << "    return eval_material_graph_" << graphSymbol << "_ctx(ctx);\n";
        src << "}\n";

        return CompileOutput {
            .shaderId      = input.shaderId,
            .vshaderSource = src.str(),
            .diagnostics   = std::move(diagnostics),
        };
    }

    std::expected<CompileOutput, std::vector<Diagnostic>>
    MeshMaterialBackend::compile(const CompileInput& input, const NodeRegistry& registry) const
    {
        // Reuse the surface-function backend to emit the graph eval functions.
        SurfaceFunctionBackend surface;
        auto                   surfaceResult = surface.compile(input, registry);
        if (!surfaceResult)
            return std::unexpected {surfaceResult.error()};

        const auto graphSymbol = sanitizeShaderId(input.shaderId);

        std::ostringstream src;
        src << "[vshader]\n";
        // The entity-id-writing variant is a distinct shader id (the project cook does
        // not expand per-shader permute keywords, so it ships as its own cooked shader).
        src << "id = \"project/material_graph/" << graphSymbol
            << (m_WriteEntityId ? ".material_eid.frag" : ".material.frag") << "\"\n";
        src << "language = glsl\n";
        src << "version = 460\n\n";
        src << "[frag]\n";
        // mesh_material.glsl gates the entity-id GBuffer output on WRITE_ENTITY_ID; the
        // selection / picking GBuffer pass writes the entity-id attachment, so the draw
        // selects this variant there (see direct_gbuffer_pass / render_system).
        if (m_WriteEntityId)
            src << "#define WRITE_ENTITY_ID 1\n";
        // Use the full embedded include path (the cook matches builtin virtual
        // includes by their canonical "include/..." path, as the preview shader's
        // "include/common/gpu_scene.glsl" does).
        src << "#include \"include/vultra/mesh_material.glsl\"\n\n";
        // The graph eval functions + any node helper #includes.
        src << surfaceResult->vshaderSource << "\n";
        // MaterialGraphSurface.shadingModel already holds the GBuffer model code (the
        // per-model output node baked it in), so pass it through unchanged.
        src << "void vultraGraphMaterial(in VultraMaterialInput IN, inout VultraMaterialEval OUT)\n{\n";
        src << "    MaterialGraphSurface s = eval_material_graph_" << graphSymbol
            << "(0u, IN.uv0, IN.positionWS, IN.normalWS, IN.viewDirWS, IN.frame.time);\n";
        src << "    OUT.baseColor = s.baseColor;\n";
        src << "    OUT.normalWS = s.normalWS;\n";
        src << "    OUT.metallic = s.metallic;\n";
        src << "    OUT.roughness = s.roughness;\n";
        src << "    OUT.ao = s.ao;\n";
        src << "    OUT.emissive = s.emissive;\n";
        src << "    OUT.alpha = s.alpha;\n";
        src << "    OUT.alphaCutoff = s.alphaCutoff;\n";
        src << "    OUT.shadingModel = s.shadingModel;\n";
        src << "}\n\n";
        src << "VULTRA_MATERIAL_MAIN(vultraGraphMaterial)\n";

        return CompileOutput {
            .shaderId      = input.shaderId,
            .vshaderSource = src.str(),
            .diagnostics   = std::move(surfaceResult->diagnostics),
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

    int compileProjectMaterialGraphs(const std::filesystem::path& projectRoot, const std::filesystem::path& assetRoot)
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        if (assetRoot.empty() || !fs::is_directory(assetRoot, ec))
            return 0;

        const auto readFile = [](const fs::path& path) -> std::string {
            std::ifstream      file(path, std::ios::binary);
            std::ostringstream text;
            text << file.rdbuf();
            return text.str();
        };

        // Builtin nodes + the project's custom .vmatnode.json descriptors.
        NodeRegistry registry = makeBuiltinNodeRegistry();
        for (auto it = fs::recursive_directory_iterator(assetRoot, ec);
             !ec && it != fs::recursive_directory_iterator();
             it.increment(ec))
        {
            if (ec || !it->is_regular_file(ec))
            {
                ec.clear();
                continue;
            }
            if (!it->path().filename().generic_string().ends_with(".vmatnode.json"))
                continue;
            auto parsed = loadNodeDescriptorFromText(readFile(it->path()));
            if (parsed.ok())
                registry.registerNode(std::move(parsed.descriptor));
        }

        const auto outDir = (projectRoot / ".vultra" / "generated" / "shaders" / "material_graph").lexically_normal();
        fs::create_directories(outDir, ec);

        const MaterialGraphCompiler  compiler {registry};
        const SurfaceFunctionBackend surfaceBackend;
        const MeshMaterialBackend    meshBackend;
        const MeshMaterialBackend    meshEntityIdBackend {true};

        int compiled = 0;
        ec.clear();
        for (auto it = fs::recursive_directory_iterator(assetRoot, ec);
             !ec && it != fs::recursive_directory_iterator();
             it.increment(ec))
        {
            if (ec || !it->is_regular_file(ec))
            {
                ec.clear();
                continue;
            }
            const auto path = it->path();
            if (!path.filename().generic_string().ends_with(".vmatgraph.json"))
                continue;

            std::vector<Diagnostic> diagnostics;
            auto                    graph = loadGraphFromText(readFile(path), &diagnostics);
            if (!graph)
            {
                VULTRA_CORE_ERROR("[MaterialGraph] Failed to load graph '{}'", path.generic_string());
                continue;
            }

            const auto shaderId = sanitizeShaderId(path.stem().generic_string());
            auto       relForId = fs::relative(path, assetRoot, ec).generic_string();
            const auto graphUri = "res://" + (ec || relForId.empty() ? path.filename().generic_string() : relForId);
            const CompileInput input {.graph = *graph, .shaderId = shaderId, .graphId = stableGraphId(graphUri)};

            // Mesh-material fragment: the shader the renderer resolves for the graph.
            auto meshResult = compiler.compile(input, meshBackend);
            if (!meshResult)
            {
                VULTRA_CORE_ERROR("[MaterialGraph] Failed to compile '{}'", path.generic_string());
                continue;
            }
            std::ofstream(outDir / (shaderId + ".material.frag.vshader"), std::ios::binary | std::ios::trunc)
                << meshResult->vshaderSource;

            // Entity-id-writing variant (selection / picking GBuffer pass).
            if (auto eidResult = compiler.compile(input, meshEntityIdBackend))
                std::ofstream(outDir / (shaderId + ".material_eid.frag.vshader"),
                              std::ios::binary | std::ios::trunc)
                    << eidResult->vshaderSource;

            // Standalone preview fragment (graph editor preview / thumbnails).
            if (auto surfaceResult = compiler.compile(input, surfaceBackend))
            {
                std::ofstream preview(outDir / (shaderId + ".frag.vshader"), std::ios::binary | std::ios::trunc);
                preview << "[vshader]\nid = \"project/material_graph/" << shaderId
                        << ".frag\"\nlanguage = glsl\nversion = 460\n\n[frag]\n";
                preview << "#extension GL_EXT_nonuniform_qualifier : require\n\n";
                preview << "#define VULTRA_DECLARE_BINDLESS_TEXTURES\n";
                preview << "#include \"include/common/gpu_scene.glsl\"\n\n";
                preview << surfaceResult->vshaderSource << "\n";
                preview << "layout(location = 0) out vec4 FragColor;\n";
                preview << "void main()\n{\n";
                preview << "    MaterialGraphSurface surface = eval_material_graph_" << shaderId
                        << "(0u, vec2(0.0), vec3(0.0), vec3(0.0, 1.0, 0.0), vec3(0.0, 0.0, 1.0), 0.0);\n";
                preview << "    FragColor = surface.baseColor;\n";
                preview << "}\n";
            }
            ++compiled;
        }
        return compiled;
    }
} // namespace vultra::material_graph
