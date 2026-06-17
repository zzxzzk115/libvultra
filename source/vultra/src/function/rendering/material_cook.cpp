// Material cooking translation unit.
//
// Split out of render_system.cpp: the
// graph/shader/builtin material cooking helpers that turn a material URI or material-graph
// into a GpuMaterial, plus their cooking caches. These were a self-contained, upstream-only
// block of render_system.cpp's anonymous namespace. The few entry points render_system.cpp
// calls are declared in render_system_internal.hpp; everything else here is file-local.
// Pure code move.

#include "vultra/function/rendering/render_system.hpp"
#include "vultra/function/material_graph/material_graph_compiler.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/engine/engine_context.hpp"
#include "vultra/core/i18n/i18n.hpp"
#include "vultra/function/debug_draw/debug_draw_interface.hpp"

#include <glm/gtc/type_ptr.hpp>
#include "vultra/core/math/math.hpp"
#include "vultra/core/services/timing_service.hpp"
#include "vultra/core/rhi/backends/webgpu/webgpu_command_buffer_access.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/deferred_deletion_queue.hpp"
#include "vultra/core/services/window_service.hpp"
#include "vultra/function/asset/builtin_assets.hpp"
#include "vultra/function/material/material_asset.hpp"
#include "vultra/function/material/material_params.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"
#include "vultra/function/framegraph/framegraph_import.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"
#include "vultra/function/rendering/framework/resource_uploader.hpp"
#include "vultra/function/rendering/render_structs.hpp"
#include "vultra/function/rendering/srp/builtin/resource_keys.hpp"
#include "vultra/function/rendering/srp/builtin/upload_resources.hpp"
#include "vultra/function/rendering/srp/declarative_renderer.hpp"
#include "vultra/function/rendering/srp/render_context.hpp"
#include "vultra/function/resource/gpu_vertex_layout.hpp"
#include "vultra/function/services/asset_service.hpp"
#include "vultra/function/services/camera_service.hpp"
#include "vultra/function/services/frame_debugger_service.hpp"
#include "vultra/function/services/gpu_resource_service.hpp"
#include "vultra/function/services/imgui_service.hpp"
#include "vultra/function/services/render_backend_service.hpp"
#include "vultra/function/services/render_upscaler_service.hpp"
#include "vultra/function/services/shader_service.hpp"
#include "vultra/function/services/world_service.hpp"
#include "vultra/function/world/components/entity_status_component.hpp"
#include "vultra/function/world/components/environment_component.hpp"
#include "vultra/function/world/components/gaussian_splat_component.hpp"
#include "vultra/function/world/components/id_component.hpp"
#include "vultra/function/world/components/layer_component.hpp"
#include "vultra/function/world/components/light_component.hpp"
#include "vultra/function/world/components/mesh_component.hpp"
#include "vultra/function/world/components/hierarchy_component.hpp"
#include "vultra/function/world/components/skin_palette_component.hpp"
#include "vultra/function/world/components/reflection_probe_component.hpp"
#include "vultra/function/world/components/transform_component.hpp"
#include "vultra/function/world/components/ui_components.hpp"
#include "vultra/function/world/world.hpp"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/packing.hpp>

#include <vbase/core/exe_path.hpp>

#include <fg/Blackboard.hpp>
#include <fg/FrameGraph.hpp>
#include <fg/GraphvizWriter.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <functional>
#include <limits>
#include <numeric>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#ifndef NDEBUG
#include <fstream>
#endif
#include "vultra/function/rendering/render_system_internal.hpp"

namespace vultra
{
    namespace rsdetail
    {
        struct GraphConstantMaterial
        {
            resource::GpuMaterialModel model {resource::GpuMaterialModel::ePBRMetallicRoughness};
            std::vector<std::byte>     bytes;
            bool                       timeDependent {false};
        };

        constexpr uint32_t kGraphConstantMaterialTag = 0xC0DEu;

        [[nodiscard]] glm::vec4 jsonVec4(const nlohmann::json& value, const glm::vec4 fallback)
        {
            if (!value.is_array())
                return fallback;
            glm::vec4 out = fallback;
            for (int i = 0; i < 4 && i < static_cast<int>(value.size()); ++i)
                if (value[i].is_number())
                    out[i] = value[i].get<float>();
            return out;
        }

        [[nodiscard]] glm::vec3 jsonVec3(const nlohmann::json& value, const glm::vec3 fallback)
        {
            const auto v = jsonVec4(value, glm::vec4(fallback, 0.0f));
            return glm::vec3(v);
        }

        [[nodiscard]] glm::vec2 jsonVec2(const nlohmann::json& value, const glm::vec2 fallback)
        {
            if (!value.is_array())
                return fallback;
            glm::vec2 out = fallback;
            for (int i = 0; i < 2 && i < static_cast<int>(value.size()); ++i)
                if (value[i].is_number())
                    out[i] = value[i].get<float>();
            return out;
        }

        [[nodiscard]] float jsonFloat(const nlohmann::json& value, const float fallback)
        {
            return value.is_number() ? value.get<float>() : fallback;
        }

        [[nodiscard]] nlohmann::json jsonVec4Value(const glm::vec4& value)
        {
            return nlohmann::json::array({value.x, value.y, value.z, value.w});
        }

        [[nodiscard]] const material_graph::Link*
        linkedInput(const material_graph::Graph& graph, const material_graph::Node& node, std::string_view pin)
        {
            return material_graph::findInputLink(graph, node.id, pin);
        }

        [[nodiscard]] bool graphDependsOnTime(const material_graph::Graph& graph,
                                              const material_graph::Node&  node,
                                              std::unordered_set<std::string>& visited)
        {
            if (!visited.insert(node.id).second)
                return false;
            if (node.typeId == "vultra.input.time")
                return true;

            for (const auto& link : graph.links)
            {
                if (link.to.nodeId != node.id)
                    continue;
                const auto* upstream = material_graph::findNode(graph, link.from.nodeId);
                if (upstream && graphDependsOnTime(graph, *upstream, visited))
                    return true;
            }
            return false;
        }

        [[nodiscard]] bool constantNodeDependsOnTime(const material_graph::Graph& graph,
                                                     const material_graph::Node&  node,
                                                     std::string_view             outputPin,
                                                     std::unordered_set<std::string>& visited)
        {
            if (!visited.insert(node.id).second)
                return false;

            if (node.typeId == "vultra.input.time" &&
                (outputPin == "seconds" || outputPin == "value" || outputPin == "out"))
                return true;
            if (node.typeId == "vultra.param.float" || node.typeId == "vultra.param.vec2" ||
                node.typeId == "vultra.param.vec3" || node.typeId == "vultra.param.vec4" ||
                node.typeId == "vultra.param.color" || node.typeId == "vultra.param.bool" ||
                node.typeId == "vultra.param.int" || node.typeId == "vultra.param.enum" ||
                node.typeId == "vultra.input.view_index" || node.typeId == "vultra.input.eye_index" ||
                node.typeId == "vultra.input.view_count" || node.typeId == "vultra.input.is_stereo_view")
            {
                return false;
            }

            auto linkedPinDepends = [&](std::string_view pin) {
                const auto* link = linkedInput(graph, node, pin);
                const auto* source = link ? material_graph::findNode(graph, link->from.nodeId) : nullptr;
                return source ? constantNodeDependsOnTime(graph, *source, link->from.pin, visited) : false;
            };

            if ((node.typeId == "vultra.math.add" || node.typeId == "vultra.math.subtract" ||
                 node.typeId == "vultra.math.multiply" || node.typeId == "vultra.math.divide" ||
                 node.typeId == "vultra.math.min" || node.typeId == "vultra.math.max") &&
                outputPin == "out")
            {
                return linkedPinDepends("a") || linkedPinDepends("b");
            }
            if ((node.typeId == "vultra.math.one_minus" || node.typeId == "vultra.math.saturate" ||
                 node.typeId == "vultra.math.sine" || node.typeId == "vultra.math.fract") &&
                outputPin == "out")
            {
                return linkedPinDepends("v");
            }
            if (node.typeId == "vultra.math.power" && outputPin == "out")
                return linkedPinDepends("base") || linkedPinDepends("exponent");
            if (node.typeId == "vultra.math.smoothstep" && outputPin == "out")
                return linkedPinDepends("edge0") || linkedPinDepends("edge1") || linkedPinDepends("x");
            if (node.typeId == "vultra.vector.split_vec2")
                return (outputPin == "x" || outputPin == "y") && linkedPinDepends("v");
            if (node.typeId == "vultra.math.mix" && outputPin == "out")
                return linkedPinDepends("a") || linkedPinDepends("b") || linkedPinDepends("t");

            return false;
        }

        [[nodiscard]] bool surfaceInputDependsOnTime(const material_graph::Graph& graph,
                                                     const material_graph::Node&  output,
                                                     std::string_view             pin)
        {
            const auto* link = linkedInput(graph, output, pin);
            const auto* source = link ? material_graph::findNode(graph, link->from.nodeId) : nullptr;
            if (!source)
                return false;

            std::unordered_set<std::string> visited;
            return constantNodeDependsOnTime(graph, *source, link->from.pin, visited);
        }

        [[nodiscard]] bool graphConstantMaterialDependsOnTime(const material_graph::Graph& graph,
                                                              const material_graph::Node&  output)
        {
            constexpr std::array<std::string_view, 11> kPackedSurfacePins {
                "baseColor",
                "emissive",
                "alpha",
                "alphaCutoff",
                "ao",
                "specular",
                "glossiness",
                "shininess",
                "metallic",
                "roughness",
                "normal",
            };

            return std::any_of(kPackedSurfacePins.begin(), kPackedSurfacePins.end(), [&](std::string_view pin) {
                return surfaceInputDependsOnTime(graph, output, pin);
            });
        }

        [[nodiscard]] const nlohmann::json*
        graphPropertyOverride(const material_graph::Graph& graph,
                              const nlohmann::json*       properties,
                              const material_graph::Node& node)
        {
            if (!properties || !properties->is_object())
                return nullptr;

            const auto findByName = [&](std::string_view name) -> const nlohmann::json* {
                if (name.empty())
                    return nullptr;
                auto it = properties->find(std::string(name));
                return it != properties->end() ? &*it : nullptr;
            };

            if (const auto* value = findByName(node.id))
                return value;
            if (const auto* value = findByName(node.displayName))
                return value;
            if (node.params.contains("name") && node.params["name"].is_string())
                if (const auto* value = findByName(node.params["name"].get<std::string>()))
                    return value;

            for (const auto& param : graph.blackboard)
            {
                if (param.name == node.id || param.name == node.displayName)
                    if (const auto* value = findByName(param.name))
                        return value;
            }
            return nullptr;
        }

        [[nodiscard]] const nlohmann::json*
        graphSurfacePropertyOverride(const nlohmann::json* properties, std::string_view pin)
        {
            if (!properties || !properties->is_object() || pin.empty())
                return nullptr;
            auto it = properties->find(std::string(pin));
            return it != properties->end() ? &*it : nullptr;
        }

        [[nodiscard]] nlohmann::json constantNodeValue(const material_graph::Graph& graph,
                                                       const material_graph::Node&  node,
                                                       std::string_view             outputPin,
                                                       const nlohmann::json&        fallback,
                                                       const float                  timeSeconds,
                                                       const nlohmann::json*        properties = nullptr)
        {
            const auto inputValue = [&](std::string_view pin, const nlohmann::json& inputFallback) {
                const auto* inputLink = linkedInput(graph, node, pin);
                const auto* inputNode = inputLink ? material_graph::findNode(graph, inputLink->from.nodeId) : nullptr;
                return inputNode ?
                           constantNodeValue(graph, *inputNode, inputLink->from.pin, inputFallback, timeSeconds, properties) :
                           inputFallback;
            };

            if (node.typeId == "vultra.param.float" || node.typeId == "vultra.param.vec2" ||
                node.typeId == "vultra.param.vec3" || node.typeId == "vultra.param.vec4" ||
                node.typeId == "vultra.param.color" || node.typeId == "vultra.param.bool" ||
                node.typeId == "vultra.param.int" || node.typeId == "vultra.param.enum")
            {
                if (const auto* value = graphPropertyOverride(graph, properties, node))
                    return *value;
                return node.params.value("value", fallback);
            }

            if (node.typeId == "vultra.input.time" &&
                (outputPin == "seconds" || outputPin == "value" || outputPin == "out"))
                return timeSeconds;
            if ((node.typeId == "vultra.input.view_index" || node.typeId == "vultra.input.eye_index") &&
                (outputPin == "index" || outputPin == "value" || outputPin == "out"))
                return 0;
            if (node.typeId == "vultra.input.view_count" &&
                (outputPin == "count" || outputPin == "value" || outputPin == "out"))
                return 1;
            if (node.typeId == "vultra.input.is_stereo_view" &&
                (outputPin == "stereo" || outputPin == "value" || outputPin == "out"))
                return false;

            if ((node.typeId == "vultra.math.add" || node.typeId == "vultra.math.subtract" ||
                 node.typeId == "vultra.math.multiply" || node.typeId == "vultra.math.divide" ||
                 node.typeId == "vultra.math.min" || node.typeId == "vultra.math.max") &&
                outputPin == "out")
            {
                const float a = jsonFloat(inputValue("a", 0.0f), 0.0f);
                const float b = jsonFloat(inputValue("b", 0.0f), 0.0f);
                if (node.typeId == "vultra.math.add")
                    return a + b;
                if (node.typeId == "vultra.math.subtract")
                    return a - b;
                if (node.typeId == "vultra.math.multiply")
                    return a * b;
                if (node.typeId == "vultra.math.divide")
                    return b == 0.0f ? 0.0f : a / b;
                if (node.typeId == "vultra.math.min")
                    return std::min(a, b);
                return std::max(a, b);
            }

            if ((node.typeId == "vultra.math.one_minus" || node.typeId == "vultra.math.power") && outputPin == "out")
            {
                if (node.typeId == "vultra.math.one_minus")
                {
                    const float v = jsonFloat(inputValue("v", 0.0f), 0.0f);
                    return 1.0f - v;
                }
                const float base     = jsonFloat(inputValue("base", 1.0f), 1.0f);
                const float exponent = jsonFloat(inputValue("exponent", 1.0f), 1.0f);
                return std::pow(std::max(base, 0.0f), exponent);
            }

            if (node.typeId == "vultra.math.saturate" && outputPin == "out")
            {
                const float v = jsonFloat(inputValue("v", 0.0f), 0.0f);
                return glm::clamp(v, 0.0f, 1.0f);
            }

            if (node.typeId == "vultra.math.sine" && outputPin == "out")
                return std::sin(jsonFloat(inputValue("v", 0.0f), 0.0f));

            if (node.typeId == "vultra.math.fract" && outputPin == "out")
            {
                const float v = jsonFloat(inputValue("v", 0.0f), 0.0f);
                return v - std::floor(v);
            }

            if (node.typeId == "vultra.math.smoothstep" && outputPin == "out")
            {
                const float edge0 = jsonFloat(inputValue("edge0", 0.0f), 0.0f);
                const float edge1 = jsonFloat(inputValue("edge1", 1.0f), 1.0f);
                const float x     = jsonFloat(inputValue("x", 0.0f), 0.0f);
                if (edge0 == edge1)
                    return x < edge0 ? 0.0f : 1.0f;
                const float t = glm::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
                return t * t * (3.0f - 2.0f * t);
            }

            if (node.typeId == "vultra.vector.split_vec2")
            {
                const glm::vec2 v = jsonVec2(inputValue("v", nlohmann::json::array({0.0f, 0.0f})), glm::vec2(0.0f));
                if (outputPin == "x")
                    return v.x;
                if (outputPin == "y")
                    return v.y;
            }

            if (node.typeId == "vultra.math.mix" && outputPin == "out")
            {
                const auto a =
                    jsonVec4(inputValue("a", nlohmann::json::array({1.0f, 1.0f, 1.0f, 1.0f})), glm::vec4(1.0f));
                const auto b =
                    jsonVec4(inputValue("b", nlohmann::json::array({1.0f, 1.0f, 1.0f, 1.0f})), glm::vec4(1.0f));
                const auto t = glm::clamp(jsonFloat(inputValue("t", 0.0f), 0.0f), 0.0f, 1.0f);
                return jsonVec4Value(glm::mix(a, b, t));
            }

            return fallback;
        }

        [[nodiscard]] nlohmann::json surfaceInputValue(const material_graph::Graph& graph,
                                                       const material_graph::Node&  output,
                                                       std::string_view             pin,
                                                       const nlohmann::json&        fallback,
                                                       const float                  timeSeconds,
                                                       const nlohmann::json*        properties = nullptr)
        {
            if (const auto* link = linkedInput(graph, output, pin))
            {
                if (const auto* source = material_graph::findNode(graph, link->from.nodeId))
                    return constantNodeValue(graph, *source, link->from.pin, fallback, timeSeconds, properties);
            }
            if (const auto* value = graphSurfacePropertyOverride(properties, pin))
                return *value;
            return output.params.value(std::string(pin), fallback);
        }

        [[nodiscard]] uint32_t materialGraphTextureIndex(IAssetService&               assets,
                                                         const material_graph::Graph& graph,
                                                         const material_graph::Node&  output,
                                                         std::string_view             pin,
                                                         const nlohmann::json*        properties = nullptr)
        {
            if (const auto* value = graphSurfacePropertyOverride(properties, pin); value && value->is_string())
            {
                auto texture = assets.loadTextureAsync(value->get<std::string>());
                return texture.ready() ? texture.gpuIndex() : 0u;
            }

            const auto* link = linkedInput(graph, output, pin);
            if (!link)
                return 0u;

            const auto* source = material_graph::findNode(graph, link->from.nodeId);
            if (!source)
                return 0u;

            std::unordered_set<std::string> visited;
            const auto findTexture = [&](const material_graph::Node& node, auto&& findTextureRef) -> uint32_t {
                if (!visited.insert(node.id).second)
                    return 0u;

                if (node.typeId == "vultra.param.texture2d")
                {
                    std::string uri = node.params.value("texture", std::string {});
                    if (const auto* value = graphPropertyOverride(graph, properties, node); value && value->is_string())
                        uri = value->get<std::string>();
                    if (uri.empty())
                        return 0u;

                    auto texture = assets.loadTextureAsync(uri);
                    return texture.ready() ? texture.gpuIndex() : 0u;
                }

                for (const auto& upstream : graph.links)
                {
                    if (upstream.to.nodeId != node.id)
                        continue;
                    const auto* upstreamNode = material_graph::findNode(graph, upstream.from.nodeId);
                    if (!upstreamNode)
                        continue;
                    if (const auto textureIndex = findTextureRef(*upstreamNode, findTextureRef); textureIndex != 0u)
                        return textureIndex;
                }
                return 0u;
            };

            return findTexture(*source, findTexture);
        }

        struct CachedMaterialGraph
        {
            uint64_t                     contentRevision {0};
            std::optional<material_graph::Graph> graph;
            std::size_t                  outputIndex {0};
            bool                         timeDependent {false};
        };

        [[nodiscard]] const CachedMaterialGraph*
        cachedMaterialGraph(IAssetService& assets, std::string_view materialGraphUri, const uint64_t contentRevision)
        {
            static std::unordered_map<std::string, CachedMaterialGraph> cache;
            auto& entry = cache[std::string(materialGraphUri)];
            if (entry.graph && entry.contentRevision == contentRevision)
                return &entry;

            entry = {};
            entry.contentRevision = contentRevision;

            std::vector<material_graph::Diagnostic> diagnostics;
            auto                                    text = assets.loadTextAssetSync(materialGraphUri);
            if (!text)
                return nullptr;

            auto graph = material_graph::loadGraphFromText(text.value(), &diagnostics);
            if (!graph)
                return nullptr;

            const auto output =
                std::find_if(graph->nodes.begin(), graph->nodes.end(), [](const material_graph::Node& node) {
                    return material_graph::isSurfaceOutputType(node.typeId);
                });
            if (output == graph->nodes.end())
                return nullptr;

            entry.outputIndex = static_cast<std::size_t>(std::distance(graph->nodes.begin(), output));
            entry.timeDependent = graphConstantMaterialDependsOnTime(*graph, *output);
            entry.graph = std::move(graph);
            return &entry;
        }

        template<typename T>
        [[nodiscard]] std::vector<std::byte> materialParamsToBytes(const T& params)
        {
            std::vector<std::byte> bytes(sizeof(T));
            std::memcpy(bytes.data(), &params, sizeof(T));
            return bytes;
        }

        // Reduces a constant-foldable material graph to the per-model GPU param block a
        // hand-authored material of that model would produce. SG/Phong stay in their own
        // authoring params (the shader collapses them to the metallic-roughness GBuffer
        // at write time). Limitations vs the compiled eShaderMaterial path: constant ao is
        // only carried by models whose block stores it (Toon); alphaMode/alphaCutoff are
        // only carried by PBR-MR. Graphs needing per-pixel control use the compiled path.
        [[nodiscard]] GraphConstantMaterial packGraphConstantMaterial(IAssetService&        assets,
                                                                      std::string_view      materialGraphUri,
                                                                      const uint64_t        contentRevision,
                                                                      const float           timeSeconds,
                                                                      const nlohmann::json* properties = nullptr)
        {
            GraphConstantMaterial out {};

            const auto* cached = cachedMaterialGraph(assets, materialGraphUri, contentRevision);
            if (!cached || !cached->graph || cached->outputIndex >= cached->graph->nodes.size())
            {
                out.bytes = materialParamsToBytes(MaterialParamsPBRMR {});
                return out;
            }
            out.timeDependent = cached->timeDependent;

            const auto& graph     = *cached->graph;
            const auto& output    = graph.nodes[cached->outputIndex];
            const auto  baseColor = jsonVec4(
                surfaceInputValue(
                    graph, output, "baseColor", nlohmann::json::array({1.0f, 1.0f, 1.0f, 1.0f}), timeSeconds, properties),
                glm::vec4(1.0f));
            const glm::vec3 emissive = jsonVec3(
                surfaceInputValue(graph, output, "emissive", nlohmann::json::array({0.0f, 0.0f, 0.0f}), timeSeconds, properties),
                glm::vec3(0.0f));
            const float alpha = glm::clamp(
                jsonFloat(surfaceInputValue(graph, output, "alpha", 1.0f, timeSeconds, properties), 1.0f), 0.0f, 1.0f);
            const float alphaCutoff = glm::clamp(
                jsonFloat(surfaceInputValue(graph, output, "alphaCutoff", 0.5f, timeSeconds, properties), 0.5f), 0.0f, 1.0f);
            const auto alphaMode = static_cast<uint32_t>(
                material_graph::alphaModeFromString(output.params.value("alphaMode", std::string {"Opaque"})));
            const uint32_t baseColorTex = materialGraphTextureIndex(assets, graph, output, "baseColor", properties);
            const float    ao           = glm::clamp(
                jsonFloat(surfaceInputValue(graph, output, "ao", 1.0f, timeSeconds, properties), 1.0f), 0.0f, 1.0f);

            const auto model = material_graph::shadingModelForOutputType(output.typeId);
            if (model == material_graph::ShadingModel::ePBRSpecularGlossiness)
            {
                MaterialParamsPBRSG p {};
                p.diffuseColor   = baseColor;
                p.specularFactor = glm::vec3(jsonVec4(
                    surfaceInputValue(graph, output, "specular", nlohmann::json::array({1.0f, 1.0f, 1.0f, 1.0f}), timeSeconds, properties),
                    glm::vec4(1.0f)));
                p.glossinessFactor = glm::clamp(
                    jsonFloat(surfaceInputValue(graph, output, "glossiness", 1.0f, timeSeconds, properties), 1.0f), 0.0f, 1.0f);
                p.diffuseColorTex = baseColorTex;
                p.emissiveFactor  = glm::vec4(emissive, 1.0f);
                out.model         = resource::GpuMaterialModel::ePBRSpecularGlossiness;
                out.bytes         = materialParamsToBytes(p);
            }
            else if (model == material_graph::ShadingModel::ePhong)
            {
                MaterialParamsPhong p {};
                p.diffuse           = baseColor;
                const glm::vec3 spec = glm::vec3(jsonVec4(
                    surfaceInputValue(graph, output, "specular", nlohmann::json::array({1.0f, 1.0f, 1.0f, 1.0f}), timeSeconds, properties),
                    glm::vec4(1.0f)));
                const float shininess = glm::max(
                    jsonFloat(surfaceInputValue(graph, output, "shininess", 32.0f, timeSeconds, properties), 32.0f), 1.0f);
                p.specularShininess = glm::vec4(spec, shininess);
                p.diffuseTex        = baseColorTex;
                p.emissiveFactor    = glm::vec4(emissive, 1.0f);
                out.model           = resource::GpuMaterialModel::ePhong;
                out.bytes           = materialParamsToBytes(p);
            }
            else if (model == material_graph::ShadingModel::eUnlit)
            {
                MaterialParamsUnlit p {};
                p.color    = glm::vec4(glm::vec3(baseColor), baseColor.a * alpha);
                p.colorTex = baseColorTex;
                out.model  = resource::GpuMaterialModel::eUnlit;
                out.bytes  = materialParamsToBytes(p);
            }
            else if (model == material_graph::ShadingModel::eToonLike)
            {
                MaterialParamsToon p {};
                p.baseColor    = baseColor;
                p.emissiveAo   = glm::vec4(emissive, ao);
                p.baseColorTex = baseColorTex;
                out.model      = resource::GpuMaterialModel::eToon;
                out.bytes      = materialParamsToBytes(p);
            }
            else // PBR Metallic-Roughness (and custom, until the BXDF path resolves it)
            {
                MaterialParamsPBRMR p {};
                p.baseColor      = baseColor;
                p.metallicFactor = glm::clamp(
                    jsonFloat(surfaceInputValue(graph, output, "metallic", 0.0f, timeSeconds, properties), 0.0f), 0.0f, 1.0f);
                p.roughnessFactor = glm::clamp(
                    jsonFloat(surfaceInputValue(graph, output, "roughness", 1.0f, timeSeconds, properties), 1.0f), 0.045f, 1.0f);
                p.alphaCutoff    = alphaCutoff;
                p.alphaMode      = alphaMode;
                p.baseColorTex   = baseColorTex;
                p.emissiveFactor = glm::vec4(emissive, 1.0f);
                out.model        = resource::GpuMaterialModel::ePBRMetallicRoughness;
                out.bytes        = materialParamsToBytes(p);
            }
            return out;
        }

        void uploadMaterialParamBlock(resource::MaterialBuffer& materialParams,
                                      rhi::RenderDevice&        rd,
                                      const uint32_t            offsetBytes,
                                      const uint32_t            sizeBytes)
        {
            if (!materialParams.gpu || sizeBytes == 0)
                return;
            if (static_cast<uint64_t>(offsetBytes) + sizeBytes > materialParams.cpu.size())
                return;

            rd.uploadS(*materialParams.gpu,
                       offsetBytes,
                       sizeBytes,
                       materialParams.cpu.data() + offsetBytes);
        }

        [[nodiscard]] std::unordered_map<std::string, uint32_t>& graphConstantMaterialIndexCache()
        {
            static std::unordered_map<std::string, uint32_t> cache;
            return cache;
        }

        [[nodiscard]] bool isGraphConstantMaterialIndex(const resource::GpuResourcePool& pool,
                                                        const uint32_t                   materialIndex,
                                                        const uint32_t                   materialInstanceId)
        {
            if (materialIndex >= pool.materials.size())
                return false;
            const auto& material = pool.materials[materialIndex];
            return material.padding == kGraphConstantMaterialTag && material.tableIndex == materialInstanceId;
        }

        struct ShaderMaterialIndexCacheEntry
        {
            uint64_t contentRevision {0};
            uint32_t materialIndex {std::numeric_limits<uint32_t>::max()};
        };

        [[nodiscard]] std::unordered_map<std::string, ShaderMaterialIndexCacheEntry>& graphShaderMaterialIndexCache()
        {
            static std::unordered_map<std::string, ShaderMaterialIndexCacheEntry> cache;
            return cache;
        }

        [[nodiscard]] bool isShaderMaterialIndex(const resource::GpuResourcePool& pool, const uint32_t materialIndex)
        {
            return materialIndex < pool.materials.size() &&
                   pool.materials[materialIndex].model == resource::GpuMaterialModel::eShaderMaterial;
        }

        void uploadGraphConstantMaterial(resource::GpuResourcePool&    pool,
                                         rhi::RenderDevice&            rd,
                                         resource::GpuMaterial&        material,
                                         const GraphConstantMaterial&  packed)
        {
            const auto size = static_cast<uint32_t>(packed.bytes.size());
            if (material.blockOffsetBytes + size <= pool.materialParams.cpu.size())
            {
                std::memcpy(pool.materialParams.cpu.data() + material.blockOffsetBytes, packed.bytes.data(), size);
                uploadMaterialParamBlock(pool.materialParams, rd, material.blockOffsetBytes, size);
            }
            else
            {
                material.blockOffsetBytes = pool.materialParams.allocAndUpload(rd, packed.bytes.data(), size);
                pool.materialTableDirty   = true;
                pool.uploadMaterialTable(rd);
            }
        }

        [[nodiscard]] uint32_t ensureMaterialGraphGpuMaterial(IAssetService&       assets,
                                                              IGpuResourceService& gpuResources,
                                                              rhi::RenderDevice&   rd,
                                                              std::string_view     materialGraphUri,
                                                              const float          timeSeconds,
                                                              const nlohmann::json* properties,
                                                              std::string_view     materialKey)
        {
            if (materialGraphUri.empty())
                return std::numeric_limits<uint32_t>::max();

            auto&          pool    = gpuResources.pool();
            const auto      keyText = materialKey.empty() ? std::string(materialGraphUri) : std::string(materialKey);
            const uint32_t materialInstanceId = material_graph::stableGraphId(keyText);
            const uint64_t contentRevision = gpuResources.contentRevision();
            auto&          indexCache = graphConstantMaterialIndexCache();

            // Graph-derived constant materials now carry a real per-model GpuMaterialModel
            // (identical to a hand-authored material). GpuMaterial.padding tags them so the
            // cache below can find them without colliding with hand-authored materials of
            // the same model. (padding is unused by the shaders.)
            auto updateExistingMaterial = [&](const uint32_t i) -> uint32_t {
                auto& material = pool.materials[i];
                struct MaterialGraphCacheEntry
                {
                    uint64_t                   contentRevision {0};
                    bool                       timeDependent {false};
                    float                      timeSeconds {std::numeric_limits<float>::quiet_NaN()};
                    uint32_t                   materialIndex {std::numeric_limits<uint32_t>::max()};
                    resource::GpuMaterialModel model {resource::GpuMaterialModel::eInvalid};
                    std::vector<std::byte>     bytes;
                };
                static std::unordered_map<std::string, MaterialGraphCacheEntry> cache;
                auto& cached = cache[keyText];
                if (!properties && cached.contentRevision == contentRevision && !cached.timeDependent)
                    return i;
                if (!properties && cached.contentRevision == contentRevision && cached.timeDependent &&
                    cached.materialIndex == i && cached.timeSeconds == timeSeconds)
                    return i;

                auto packed = packGraphConstantMaterial(assets, materialGraphUri, contentRevision, timeSeconds, properties);
                const auto size = static_cast<uint32_t>(packed.bytes.size());
                const bool blockUnchanged =
                    material.model == packed.model &&
                    material.blockOffsetBytes + size <= pool.materialParams.cpu.size() &&
                    std::memcmp(pool.materialParams.cpu.data() + material.blockOffsetBytes, packed.bytes.data(), size) == 0;
                if (!blockUnchanged)
                {
                    if (material.model != packed.model)
                    {
                        material.model          = packed.model;
                        pool.materialTableDirty = true;
                    }
                    uploadGraphConstantMaterial(pool, rd, material, packed);
                    if (pool.materialTableDirty)
                        pool.uploadMaterialTable(rd);
                }
                cached = {
                    .contentRevision = contentRevision,
                    .timeDependent   = packed.timeDependent,
                    .timeSeconds     = timeSeconds,
                    .materialIndex   = i,
                    .model           = packed.model,
                    .bytes           = std::move(packed.bytes),
                };
                return i;
            };

            if (const auto cachedIndex = indexCache.find(keyText); cachedIndex != indexCache.end())
            {
                if (isGraphConstantMaterialIndex(pool, cachedIndex->second, materialInstanceId))
                    return updateExistingMaterial(cachedIndex->second);
                indexCache.erase(cachedIndex);
            }

            for (uint32_t i = 0; i < static_cast<uint32_t>(pool.materials.size()); ++i)
            {
                auto& material = pool.materials[i];
                if (material.padding == kGraphConstantMaterialTag && material.tableIndex == materialInstanceId)
                {
                    indexCache[keyText] = i;
                    return updateExistingMaterial(i);
                }
            }

            auto                  packed = packGraphConstantMaterial(assets, materialGraphUri, contentRevision, timeSeconds, properties);
            resource::GpuMaterial material;
            material.model            = packed.model;
            material.blockOffsetBytes = pool.materialParams.allocAndUpload(
                rd, packed.bytes.data(), static_cast<uint32_t>(packed.bytes.size()));
            material.tableIndex       = materialInstanceId;
            material.padding          = kGraphConstantMaterialTag;

            const uint32_t index = static_cast<uint32_t>(pool.materials.size());
            pool.materials.push_back(material);
            indexCache[keyText] = index;
            pool.materialTableDirty = true;
            pool.uploadMaterialTable(rd);
            gpuResources.markContentDirty();
            return index;
        }

        [[nodiscard]] bool hasGraphConstantGpuMaterial(IGpuResourceService& gpuResources, std::string_view materialKey)
        {
            if (materialKey.empty())
                return false;

            const uint32_t materialInstanceId = material_graph::stableGraphId(std::string(materialKey));
            const auto&    pool               = gpuResources.pool();
            auto&          indexCache         = graphConstantMaterialIndexCache();
            if (const auto cachedIndex = indexCache.find(std::string(materialKey)); cachedIndex != indexCache.end())
            {
                if (isGraphConstantMaterialIndex(pool, cachedIndex->second, materialInstanceId))
                    return true;
                indexCache.erase(cachedIndex);
            }
            return std::any_of(pool.materials.begin(), pool.materials.end(), [materialInstanceId](const auto& material) {
                return material.padding == kGraphConstantMaterialTag && material.tableIndex == materialInstanceId;
            });
        }

        [[nodiscard]] uint32_t stableMaterialAssetId(std::string_view uri)
        {
            uint32_t hash = 2166136261u;
            for (const unsigned char ch : uri)
            {
                hash ^= ch;
                hash *= 16777619u;
            }
            return hash == 0u ? 1u : hash;
        }

        [[nodiscard]] std::optional<MaterialParamsPBRMR> builtinPbrMaterialParamsFromAsset(IAssetService& assets,
                                                                                           std::string_view materialUri,
                                                                                           const nlohmann::json* overrides = nullptr)
        {
            auto text = assets.loadTextAssetSync(materialUri);
            if (!text)
                return std::nullopt;

            nlohmann::json root;
            try
            {
                root = nlohmann::json::parse(text.value());
            }
            catch (const nlohmann::json::exception&)
            {
                return std::nullopt;
            }

            if (!root.is_object() || root.value("type", std::string {}) != "Material")
                return std::nullopt;

            const auto parsed = material::materialAssetFromJson(root);
            if (!parsed.ok() || parsed.asset.source.kind != material::MaterialSourceKind::eBuiltin ||
                parsed.asset.source.id != "builtin/pbr")
            {
                return std::nullopt;
            }

            const auto& properties = root["properties"];
            MaterialParamsPBRMR params;
            const auto textureIndex = [&](const nlohmann::json& sourceProperties, const char* key) -> uint32_t {
                if (!sourceProperties.is_object())
                    return 0u;
                const auto uri = sourceProperties.value(key, std::string {});
                if (uri.empty())
                    return 0u;
                auto texture = assets.loadTextureAsync(uri);
                return texture.ready() ? texture.gpuIndex() : 0u;
            };
            const auto applyTextureProperties = [&](const nlohmann::json& sourceProperties) {
                if (!sourceProperties.is_object())
                    return;
                if (auto index = textureIndex(sourceProperties, "baseColorTexture"); index != 0u)
                    params.baseColorTex = index;
                if (auto index = textureIndex(sourceProperties, "normalTexture"); index != 0u)
                    params.normalTex = index;
                if (auto index = textureIndex(sourceProperties, "metallicRoughnessTexture"); index != 0u)
                {
                    params.mrTex         = index;
                    params.mrTextureMode = 0u;
                }
                if (auto index = textureIndex(sourceProperties, "metallicTexture"); index != 0u)
                    params.metallicTex = index;
                if (auto index = textureIndex(sourceProperties, "roughnessTexture"); index != 0u)
                    params.roughnessTex = index;
                if (auto index = textureIndex(sourceProperties, "ambientOcclusionTexture"); index != 0u)
                    params.occlusionTex = index;
                if (auto index = textureIndex(sourceProperties, "emissiveTexture"); index != 0u)
                    params.emissiveTex = index;
            };
            const auto applyEmissiveProperties = [&](const nlohmann::json& sourceProperties) {
                if (!sourceProperties.is_object())
                    return;
                glm::vec3 color    = glm::vec3(params.emissiveFactor);
                float     strength = 1.0f;
                if (sourceProperties.contains("emissiveColor"))
                    color = glm::vec3(jsonVec4(sourceProperties.value("emissiveColor", nlohmann::json::array()),
                                               glm::vec4(color, 1.0f)));
                if (sourceProperties.contains("emissiveStrength"))
                    strength = jsonFloat(sourceProperties.value("emissiveStrength", strength), strength);
                params.emissiveFactor = glm::vec4(color * strength, 1.0f);
            };
            if (properties.is_object())
            {
                params.baseColor       = jsonVec4(properties.value("baseColor", nlohmann::json::array()), params.baseColor);
                params.metallicFactor  = glm::clamp(jsonFloat(properties.value("metallic", params.metallicFactor),
                                                             params.metallicFactor),
                                                   0.0f,
                                                   1.0f);
                params.roughnessFactor = glm::clamp(jsonFloat(properties.value("roughness", params.roughnessFactor),
                                                             params.roughnessFactor),
                                                   0.045f,
                                                   1.0f);
                params.alphaCutoff     = glm::clamp(jsonFloat(properties.value("alphaCutoff", params.alphaCutoff),
                                                             params.alphaCutoff),
                                                   0.0f,
                                                   1.0f);
                params.doubleSided     = properties.value("doubleSided", false) ? 1u : 0u;
                applyEmissiveProperties(properties);
                applyTextureProperties(properties);
            }
            if (overrides && overrides->is_object())
            {
                params.baseColor       = jsonVec4(overrides->value("baseColor", nlohmann::json::array()), params.baseColor);
                params.metallicFactor  = glm::clamp(jsonFloat(overrides->value("metallic", params.metallicFactor),
                                                             params.metallicFactor),
                                                   0.0f,
                                                   1.0f);
                params.roughnessFactor = glm::clamp(jsonFloat(overrides->value("roughness", params.roughnessFactor),
                                                             params.roughnessFactor),
                                                   0.045f,
                                                   1.0f);
                params.alphaCutoff     = glm::clamp(jsonFloat(overrides->value("alphaCutoff", params.alphaCutoff),
                                                             params.alphaCutoff),
                                                   0.0f,
                                                   1.0f);
                applyEmissiveProperties(*overrides);
                applyTextureProperties(*overrides);
            }
            return params;
        }

        [[nodiscard]] uint32_t ensurePbrGpuMaterial(IGpuResourceService&      gpuResources,
                                                    rhi::RenderDevice&        rd,
                                                    const uint32_t            materialId,
                                                    const MaterialParamsPBRMR& params)
        {
            auto& pool = gpuResources.pool();
            for (uint32_t i = 0; i < static_cast<uint32_t>(pool.materials.size()); ++i)
            {
                auto& material = pool.materials[i];
                if (material.model != resource::GpuMaterialModel::ePBRMetallicRoughness ||
                    material.tableIndex != materialId)
                {
                    continue;
                }

                if (material.blockOffsetBytes + sizeof(params) <= pool.materialParams.cpu.size() &&
                    std::memcmp(pool.materialParams.cpu.data() + material.blockOffsetBytes, &params, sizeof(params)) == 0)
                {
                    return i;
                }

                if (material.blockOffsetBytes + sizeof(params) <= pool.materialParams.cpu.size())
                {
                    std::memcpy(pool.materialParams.cpu.data() + material.blockOffsetBytes, &params, sizeof(params));
                    uploadMaterialParamBlock(pool.materialParams,
                                             rd,
                                             material.blockOffsetBytes,
                                             static_cast<uint32_t>(sizeof(params)));
                }
                else
                {
                    material.blockOffsetBytes = pool.materialParams.allocAndUpload(rd, &params, sizeof(params), 16);
                    pool.materialTableDirty   = true;
                    pool.uploadMaterialTable(rd);
                }
                return i;
            }

            resource::GpuMaterial material;
            material.model            = resource::GpuMaterialModel::ePBRMetallicRoughness;
            material.blockOffsetBytes = pool.materialParams.allocAndUpload(rd, &params, sizeof(params), 16);
            material.tableIndex       = materialId;

            const uint32_t index = static_cast<uint32_t>(pool.materials.size());
            pool.materials.push_back(material);
            pool.materialTableDirty = true;
            pool.uploadMaterialTable(rd);
            gpuResources.markContentDirty();
            return index;
        }

        [[nodiscard]] uint32_t ensureBuiltinMaterialAssetGpuMaterial(IAssetService&       assets,
                                                                     IGpuResourceService& gpuResources,
                                                                     rhi::RenderDevice&   rd,
                                                                     std::string_view     materialUri,
                                                                     const nlohmann::json* overrides)
        {
            if (materialUri.empty())
                return std::numeric_limits<uint32_t>::max();

            auto params = builtinPbrMaterialParamsFromAsset(assets, materialUri, overrides);
            if (!params)
                return std::numeric_limits<uint32_t>::max();

            const uint32_t materialId =
                overrides && overrides->is_object() && !overrides->empty() ?
                    material_graph::stableGraphId(std::string(materialUri) + "#" + overrides->dump()) :
                    stableMaterialAssetId(materialUri);
            return ensurePbrGpuMaterial(gpuResources, rd, materialId, *params);
        }

        struct GraphMaterialAssetSource
        {
            std::string   graphUri;
            nlohmann::json properties {nlohmann::json::object()};
        };

        [[nodiscard]] std::optional<GraphMaterialAssetSource> graphMaterialSourceFromAsset(IAssetService& assets,
                                                                                           std::string_view materialUri)
        {
            auto text = assets.loadTextAssetSync(materialUri);
            if (!text)
                return std::nullopt;

            nlohmann::json root;
            try
            {
                root = nlohmann::json::parse(text.value());
            }
            catch (const nlohmann::json::exception&)
            {
                return std::nullopt;
            }

            if (!root.is_object() || root.value("type", std::string {}) != "Material")
                return std::nullopt;

            const auto parsed = material::materialAssetFromJson(root);
            if (!parsed.ok() || parsed.asset.source.kind != material::MaterialSourceKind::eGraph)
                return std::nullopt;

            GraphMaterialAssetSource out;
            out.graphUri = parsed.asset.source.uri;
            if (out.graphUri.empty())
                return std::nullopt;
            if (const auto& properties = root["properties"]; properties.is_object())
                out.properties = properties;
            return out;
        }

        struct ShaderMaterialAssetSource
        {
            material::MaterialSourceRef source;
            nlohmann::json              properties {nlohmann::json::object()};
        };

        [[nodiscard]] std::optional<ShaderMaterialAssetSource> shaderMaterialSourceFromAsset(IAssetService& assets,
                                                                                             std::string_view materialUri)
        {
            auto text = assets.loadTextAssetSync(materialUri);
            if (!text)
                return std::nullopt;

            nlohmann::json root;
            try
            {
                root = nlohmann::json::parse(text.value());
            }
            catch (const nlohmann::json::exception&)
            {
                return std::nullopt;
            }

            if (!root.is_object() || root.value("type", std::string {}) != "Material")
                return std::nullopt;

            const auto parsed = material::materialAssetFromJson(root);
            if (!parsed.ok() || parsed.asset.source.kind != material::MaterialSourceKind::eShader ||
                parsed.asset.source.id.empty())
            {
                return std::nullopt;
            }

            ShaderMaterialAssetSource out;
            out.source = parsed.asset.source;
            if (const auto& properties = root["properties"]; properties.is_object())
                out.properties = properties;
            return out;
        }

        [[nodiscard]] material::MaterialSourceSchema resolveShaderMaterialSchema(IShaderService*                     shaders,
                                                                                 const material::MaterialSourceRef& source)
        {
            if (!shaders || source.id.empty())
                return {};

            rhi::ShaderLibraryRuntime* library = nullptr;
            if (source.shaderLibrary == "builtin")
            {
                library = &shaders->builtinLibrary();
            }
            else
            {
                library = shaders->findProjectLibrary("res://shaders/project.vshaderlib.lua");
                if (!library)
                    library = shaders->loadProjectLibrary("res://shaders/project.vshaderlib.lua");
            }
            if (!library)
                return {};

            const auto variantHash =
                rhi::ShaderLibraryRuntime::computeVariantHash(source.id, vshadersystem::ShaderStage::eFrag, {});
            if (!library->hasVariant(variantHash, vshadersystem::ShaderStage::eFrag))
                return {};

            auto shader = library->load(variantHash, vshadersystem::ShaderStage::eFrag);
            return shader ? material::materialSourceSchemaFromShaderMaterialDescription(shader->materialDesc) :
                            material::MaterialSourceSchema {};
        }

        struct ResolvedShaderMaterialVariant
        {
            rhi::ShaderLibraryRuntime*              library {nullptr};
            std::string                             libraryUri;
            uint64_t                                variantHash {0};
            // Entity-id-writing sibling variant ("...material_eid.frag"), if cooked (0 otherwise).
            uint64_t                                entityIdVariantHash {0};
            vshadersystem::MaterialDescription      materialDesc;
        };

        void warnShaderMaterialOnce(std::string_view materialUri, const std::string& message)
        {
            static std::unordered_set<std::string> s_Seen;
            auto key = std::string(materialUri) + "|" + message;
            if (!s_Seen.insert(key).second)
                return;
            VULTRA_CORE_WARN("[RenderSystem] Shader material '{}': {}", materialUri, message);
        }

        [[nodiscard]] std::optional<ResolvedShaderMaterialVariant>
        resolveShaderMaterialVariant(IShaderService* shaders, const material::MaterialSourceRef& source)
        {
            if (!shaders || source.id.empty())
                return std::nullopt;

            ResolvedShaderMaterialVariant out;
            if (source.shaderLibrary == "builtin")
            {
                out.library    = &shaders->builtinLibrary(rhi::ShaderProfile::eHighend);
                out.libraryUri = "builtin";
            }
            else
            {
                out.libraryUri = source.shaderLibrary.empty() ? "res://shaders/project.vshaderlib.lua" :
                                                               source.shaderLibrary;
                out.library = shaders->findProjectLibrary(out.libraryUri);
                if (!out.library)
                    out.library = shaders->loadProjectLibrary(out.libraryUri);
            }
            if (!out.library)
                return std::nullopt;

            out.variantHash =
                rhi::ShaderLibraryRuntime::computeVariantHash(source.id, vshadersystem::ShaderStage::eFrag, {});
            if (!out.library->hasVariant(out.variantHash, vshadersystem::ShaderStage::eFrag))
                return std::nullopt;

            // Graph mesh-material fragments ship a sibling entity-id-writing variant under
            // "<id-with-.material_eid.frag>"; resolve it for the selection / picking pass.
            if (const auto pos = source.id.rfind(".material.frag"); pos != std::string::npos)
            {
                auto eidId = source.id;
                eidId.replace(pos, std::string_view {".material.frag"}.size(), ".material_eid.frag");
                const auto entityIdHash =
                    rhi::ShaderLibraryRuntime::computeVariantHash(eidId, vshadersystem::ShaderStage::eFrag, {});
                if (out.library->hasVariant(entityIdHash, vshadersystem::ShaderStage::eFrag))
                    out.entityIdVariantHash = entityIdHash;
            }

            auto shader = out.library->load(out.variantHash, vshadersystem::ShaderStage::eFrag);
            if (!shader)
                return std::nullopt;
            out.materialDesc = std::move(shader->materialDesc);
            return out;
        }

        [[nodiscard]] std::string lowerAscii(std::string_view text)
        {
            std::string out;
            out.reserve(text.size());
            for (const unsigned char ch : text)
                out.push_back(static_cast<char>(std::tolower(ch)));
            return out;
        }

        [[nodiscard]] bool nameMatchesAny(std::string_view name, std::initializer_list<std::string_view> candidates)
        {
            const auto lowered = lowerAscii(name);
            for (const auto candidate : candidates)
            {
                if (lowered == candidate)
                    return true;
            }
            return false;
        }

        [[nodiscard]] glm::vec4 schemaVec4Default(const material::MaterialPropertySchema& property,
                                                  const glm::vec4                         fallback)
        {
            if (const auto* value = std::get_if<glm::vec4>(&property.defaultValue))
                return *value;
            if (const auto* value = std::get_if<glm::vec3>(&property.defaultValue))
                return glm::vec4(*value, fallback.w);
            return fallback;
        }

        [[nodiscard]] float schemaFloatDefault(const material::MaterialPropertySchema& property, const float fallback)
        {
            if (const auto* value = std::get_if<float>(&property.defaultValue))
                return *value;
            if (const auto* value = std::get_if<int32_t>(&property.defaultValue))
                return static_cast<float>(*value);
            return fallback;
        }

        [[nodiscard]] std::string textureUriProperty(const nlohmann::json& properties, std::string_view key)
        {
            if (!properties.is_object())
                return {};
            const auto it = properties.find(std::string(key));
            return it != properties.end() && it->is_string() ? it->get<std::string>() : std::string {};
        }

        [[nodiscard]] uint32_t shaderParamTypeByteSize(const vshadersystem::ParamType type)
        {
            switch (type)
            {
                case vshadersystem::ParamType::eVec2:
                    return sizeof(glm::vec2);
                case vshadersystem::ParamType::eVec3:
                    return sizeof(glm::vec3);
                case vshadersystem::ParamType::eVec4:
                    return sizeof(glm::vec4);
                case vshadersystem::ParamType::eMat3:
                    return sizeof(glm::mat3);
                case vshadersystem::ParamType::eMat4:
                    return sizeof(glm::mat4);
                case vshadersystem::ParamType::eInt:
                case vshadersystem::ParamType::eUInt:
                case vshadersystem::ParamType::eBool:
                case vshadersystem::ParamType::eFloat:
                default:
                    return sizeof(uint32_t);
            }
        }

        [[nodiscard]] const char* shaderParamTypeName(const vshadersystem::ParamType type)
        {
            switch (type)
            {
                case vshadersystem::ParamType::eFloat:
                    return "float";
                case vshadersystem::ParamType::eVec2:
                    return "vec2";
                case vshadersystem::ParamType::eVec3:
                    return "vec3";
                case vshadersystem::ParamType::eVec4:
                    return "vec4";
                case vshadersystem::ParamType::eInt:
                    return "int";
                case vshadersystem::ParamType::eUInt:
                    return "uint";
                case vshadersystem::ParamType::eBool:
                    return "bool";
                case vshadersystem::ParamType::eMat3:
                    return "mat3";
                case vshadersystem::ParamType::eMat4:
                    return "mat4";
                default:
                    return "unknown";
            }
        }

        [[nodiscard]] bool shaderParamTypeIsJsonWritable(const vshadersystem::ParamType type)
        {
            switch (type)
            {
                case vshadersystem::ParamType::eVec2:
                case vshadersystem::ParamType::eVec3:
                case vshadersystem::ParamType::eVec4:
                case vshadersystem::ParamType::eInt:
                case vshadersystem::ParamType::eUInt:
                case vshadersystem::ParamType::eBool:
                case vshadersystem::ParamType::eFloat:
                    return true;
                case vshadersystem::ParamType::eMat3:
                case vshadersystem::ParamType::eMat4:
                default:
                    return false;
            }
        }

        template<typename T>
        void writeShaderMaterialParam(std::vector<std::byte>& bytes,
                                      const vshadersystem::MaterialParamDesc& param,
                                      const T& value)
        {
            if (param.offset >= bytes.size())
                return;
            const size_t count = std::min<size_t>({sizeof(T), param.size, bytes.size() - param.offset});
            std::memcpy(bytes.data() + param.offset, &value, count);
        }

        void writeShaderMaterialDefault(std::vector<std::byte>& bytes, const vshadersystem::MaterialParamDesc& param)
        {
            if (!param.hasDefault || param.offset >= bytes.size())
                return;
            const size_t count = std::min<size_t>({sizeof(param.defaultValue.valueBuffer),
                                                   param.size,
                                                   bytes.size() - param.offset});
            std::memcpy(bytes.data() + param.offset, param.defaultValue.valueBuffer, count);
        }

        void writeShaderMaterialJsonValue(std::vector<std::byte>&              bytes,
                                          const vshadersystem::MaterialParamDesc& param,
                                          const nlohmann::json&                value)
        {
            switch (param.type)
            {
                case vshadersystem::ParamType::eVec2:
                    writeShaderMaterialParam(bytes, param, jsonVec2(value, glm::vec2 {0.0f}));
                    break;
                case vshadersystem::ParamType::eVec3:
                    writeShaderMaterialParam(bytes, param, jsonVec3(value, glm::vec3 {0.0f}));
                    break;
                case vshadersystem::ParamType::eVec4:
                    writeShaderMaterialParam(bytes, param, jsonVec4(value, glm::vec4 {0.0f}));
                    break;
                case vshadersystem::ParamType::eInt:
                    writeShaderMaterialParam(bytes, param, value.is_number_integer() ? value.get<int32_t>() : int32_t {0});
                    break;
                case vshadersystem::ParamType::eUInt:
                {
                    const uint32_t out = value.is_number_unsigned() ? value.get<uint32_t>() :
                                         value.is_number_integer() ?
                                             static_cast<uint32_t>(std::max(value.get<int32_t>(), 0)) :
                                             0u;
                    writeShaderMaterialParam(bytes, param, out);
                    break;
                }
                case vshadersystem::ParamType::eBool:
                    writeShaderMaterialParam(bytes, param, value.is_boolean() && value.get<bool>() ? 1u : 0u);
                    break;
                case vshadersystem::ParamType::eFloat:
                    writeShaderMaterialParam(bytes, param, jsonFloat(value, 0.0f));
                    break;
                case vshadersystem::ParamType::eMat3:
                case vshadersystem::ParamType::eMat4:
                default:
                    break;
            }
        }

        struct ShaderMaterialParamPackResult
        {
            std::vector<std::byte>  bytes;
            std::vector<std::string> diagnostics;
            // Bytes actually declared by the shader's material block (0 when the
            // shader has no [properties]). Distinct from bytes.size(), which is
            // padded up to a 16-byte SSBO minimum for allocation. Callers use this
            // to decide whether the fragment declares a set=1,binding=1 material
            // block at all -- binding that descriptor when the shader lacks it
            // corrupts set 1 (drops draw params -> zeroed transforms).
            uint32_t declaredSize {0u};
        };

        [[nodiscard]] ShaderMaterialParamPackResult
        packShaderMaterialParams(IAssetService&                             assets,
                                 const vshadersystem::MaterialDescription& desc,
                                 const nlohmann::json&                     properties)
        {
            uint32_t size = desc.materialParamSize;
            for (const auto& param : desc.params)
                size = std::max(size, param.offset + std::max(param.size, shaderParamTypeByteSize(param.type)));

            ShaderMaterialParamPackResult result;
            result.declaredSize = size;
            auto& bytes = result.bytes;
            bytes.resize(std::max<uint32_t>(size, 16u), std::byte {0});
            for (const auto& param : desc.params)
                writeShaderMaterialDefault(bytes, param);

            if (!properties.is_object())
                return result;

            std::unordered_set<std::string> knownProperties;
            for (const auto& param : desc.params)
            {
                if (auto textureName = material::shaderTexturePropertyNameFromIndexParam(param))
                    knownProperties.insert(*textureName);
                else
                    knownProperties.insert(param.name);
            }

            for (const auto& param : desc.params)
            {
                if (auto textureName = material::shaderTexturePropertyNameFromIndexParam(param))
                {
                    const auto it = properties.find(*textureName);
                    if (it == properties.end())
                        continue;
                    if (!it->is_string())
                    {
                        result.diagnostics.push_back("property '" + *textureName + "' expects a texture URI string");
                        continue;
                    }
                    const auto uri = it->get<std::string>();
                    if (uri.empty())
                        continue;
                    auto texture = assets.loadTextureAsync(uri);
                    const uint32_t index = texture.ready() ? texture.gpuIndex() : 0u;
                    writeShaderMaterialParam(bytes, param, index);
                    continue;
                }

                const auto it = properties.find(param.name);
                if (it == properties.end())
                    continue;
                if (!shaderParamTypeIsJsonWritable(param.type))
                {
                    result.diagnostics.push_back("property '" + param.name + "' uses unsupported reflected type '" +
                                                 shaderParamTypeName(param.type) + "'");
                    continue;
                }
                writeShaderMaterialJsonValue(bytes, param, *it);
            }

            for (const auto& [key, value] : properties.items())
            {
                static_cast<void>(value);
                if (!knownProperties.contains(key))
                    result.diagnostics.push_back("unknown property '" + key + "' is not declared by the shader");
            }
            return result;
        }

        [[nodiscard]] uint32_t ensureShaderMaterialAssetGpuMaterial(IAssetService&       assets,
                                                                    IShaderService*      shaders,
                                                                    IGpuResourceService& gpuResources,
                                                                    rhi::RenderDevice&   rd,
                                                                    std::string_view     materialUri,
                                                                    const nlohmann::json* overrides = nullptr,
                                                                    std::string_view     materialKey = {})
        {
            auto shaderSource = shaderMaterialSourceFromAsset(assets, materialUri);
            if (!shaderSource)
                return std::numeric_limits<uint32_t>::max();

            auto resolved = resolveShaderMaterialVariant(shaders, shaderSource->source);
            if (!resolved)
            {
                warnShaderMaterialOnce(std::string(materialUri),
                                       "missing fragment shader variant '" + shaderSource->source.id +
                                           "' in library '" +
                                           (shaderSource->source.shaderLibrary.empty() ?
                                                std::string("res://shaders/project.vshaderlib.lua") :
                                                shaderSource->source.shaderLibrary) +
                                           "'");
                return std::numeric_limits<uint32_t>::max();
            }

            nlohmann::json properties = shaderSource->properties.is_object() ? shaderSource->properties :
                                                                               nlohmann::json::object();
            if (overrides && overrides->is_object())
            {
                for (const auto& [key, value] : overrides->items())
                    properties[key] = value;
            }

            auto& pool = gpuResources.pool();
            const auto keyText = materialKey.empty() ? std::string(materialUri) : std::string(materialKey);
            const uint32_t materialInstanceId =
                material_graph::stableGraphId(keyText + "#shader-material#" + std::to_string(resolved->variantHash));
            auto packed = packShaderMaterialParams(assets, resolved->materialDesc, properties);
            for (const auto& diagnostic : packed.diagnostics)
                warnShaderMaterialOnce(std::string(materialUri), diagnostic);
            auto& bytes = packed.bytes;

            for (uint32_t i = 0; i < static_cast<uint32_t>(pool.materials.size()); ++i)
            {
                auto& material = pool.materials[i];
                if (material.model != resource::GpuMaterialModel::eShaderMaterial ||
                    material.tableIndex != materialInstanceId)
                {
                    continue;
                }

                if (material.blockOffsetBytes + bytes.size() <= pool.materialParams.cpu.size() &&
                    std::memcmp(pool.materialParams.cpu.data() + material.blockOffsetBytes, bytes.data(), bytes.size()) == 0)
                {
                    pool.shaderMaterials[material.tableIndex] = resource::ShaderMaterialRuntimeInfo {
                        .shaderLibraryUri     = resolved->libraryUri,
                        .fragmentShaderId     = shaderSource->source.id,
                        .fragmentVariantHash  = resolved->variantHash,
                        .fragmentVariantHashEntityId = resolved->entityIdVariantHash,
                        .materialParamSize    = packed.declaredSize,
                    };
                    return i;
                }

                if (material.blockOffsetBytes + bytes.size() <= pool.materialParams.cpu.size())
                {
                    std::memcpy(pool.materialParams.cpu.data() + material.blockOffsetBytes, bytes.data(), bytes.size());
                    uploadMaterialParamBlock(pool.materialParams,
                                             rd,
                                             material.blockOffsetBytes,
                                             static_cast<uint32_t>(bytes.size()));
                }
                else
                {
                    material.blockOffsetBytes =
                        pool.materialParams.allocAndUpload(rd, bytes.data(), static_cast<uint32_t>(bytes.size()), 16);
                    pool.materialTableDirty = true;
                    pool.uploadMaterialTable(rd);
                }
                pool.shaderMaterials[material.tableIndex] = resource::ShaderMaterialRuntimeInfo {
                    .shaderLibraryUri     = resolved->libraryUri,
                    .fragmentShaderId     = shaderSource->source.id,
                    .fragmentVariantHash  = resolved->variantHash,
                    .fragmentVariantHashEntityId = resolved->entityIdVariantHash,
                    .materialParamSize    = packed.declaredSize,
                };
                return i;
            }

            resource::GpuMaterial material;
            material.model            = resource::GpuMaterialModel::eShaderMaterial;
            material.blockOffsetBytes =
                pool.materialParams.allocAndUpload(rd, bytes.data(), static_cast<uint32_t>(bytes.size()), 16);
            material.tableIndex = materialInstanceId;
            material.padding    = static_cast<uint32_t>(resolved->variantHash);

            const uint32_t index = static_cast<uint32_t>(pool.materials.size());
            pool.materials.push_back(material);
            pool.shaderMaterials[material.tableIndex] = resource::ShaderMaterialRuntimeInfo {
                .shaderLibraryUri     = resolved->libraryUri,
                .fragmentShaderId     = shaderSource->source.id,
                .fragmentVariantHash  = resolved->variantHash,
                .fragmentVariantHashEntityId = resolved->entityIdVariantHash,
                .materialParamSize    = packed.declaredSize,
            };
            pool.materialTableDirty = true;
            pool.uploadMaterialTable(rd);
            gpuResources.markContentDirty();
            return index;
        }

        // Enabler A: render a material GRAPH through its compiled per-pixel GLSL by
        // routing it to the eShaderMaterial path, pointing at the mesh-material
        // fragment the editor cooks to .vultra/generated/shaders/material_graph/.
        // Only triggers when that cooked fragment variant exists (i.e. the graph was
        // compiled with the current editor); otherwise returns max() so the caller
        // falls back to the parametric path. Portable: the cooked variant carries
        // both SPIR-V and WGSL.
        [[nodiscard]] uint32_t ensureMaterialGraphShaderMaterial(IAssetService&        assets,
                                                                 IShaderService*       shaders,
                                                                 IGpuResourceService&  gpuResources,
                                                                 rhi::RenderDevice&    rd,
                                                                 std::string_view      graphUri,
                                                                 const nlohmann::json* properties,
                                                                 std::string_view      materialKey)
        {
            if (!shaders || graphUri.empty())
                return std::numeric_limits<uint32_t>::max();

            auto&      pool            = gpuResources.pool();
            const auto keyText         = materialKey.empty() ? std::string(graphUri) : std::string(materialKey);
            const auto contentRevision = gpuResources.contentRevision();
            auto&      indexCache      = graphShaderMaterialIndexCache();
            if (!properties)
            {
                if (const auto cachedIndex = indexCache.find(keyText); cachedIndex != indexCache.end())
                {
                    if (cachedIndex->second.contentRevision == contentRevision &&
                        isShaderMaterialIndex(pool, cachedIndex->second.materialIndex))
                    {
                        return cachedIndex->second.materialIndex;
                    }
                    indexCache.erase(cachedIndex);
                }
            }

            const auto sym =
                material_graph::sanitizeShaderId(std::filesystem::path(std::string(graphUri)).stem().generic_string());
            // The mesh-material backend emits an explicit [vshader] id of
            // "project/material_graph/<sym>.material.frag" (MeshMaterialBackend),
            // which is the cooked variant id we resolve here.
            const std::vector<std::string> candidateIds {
                "project/material_graph/" + sym + ".material.frag",
            };

            std::optional<ResolvedShaderMaterialVariant> resolved;
            material::MaterialSourceRef                   source;
            source.shaderLibrary = "res://shaders/project.vshaderlib.lua";
            for (const auto& id : candidateIds)
            {
                source.id = id;
                resolved  = resolveShaderMaterialVariant(shaders, source);
                if (resolved)
                    break;
            }
            if (!resolved)
                return std::numeric_limits<uint32_t>::max(); // not compiled -> parametric fallback

            const uint32_t materialInstanceId =
                material_graph::stableGraphId(keyText + "#graph-shader#" + std::to_string(resolved->variantHash));

            auto packed = packShaderMaterialParams(
                assets, resolved->materialDesc, properties ? *properties : nlohmann::json::object());
            auto& bytes = packed.bytes;

            const auto makeRuntimeInfo = [&] {
                return resource::ShaderMaterialRuntimeInfo {
                    .shaderLibraryUri    = resolved->libraryUri,
                    .fragmentShaderId    = source.id,
                    .fragmentVariantHash = resolved->variantHash,
                    .fragmentVariantHashEntityId = resolved->entityIdVariantHash,
                    .materialParamSize   = packed.declaredSize,
                };
            };

            for (uint32_t i = 0; i < static_cast<uint32_t>(pool.materials.size()); ++i)
            {
                auto& material = pool.materials[i];
                if (material.model != resource::GpuMaterialModel::eShaderMaterial ||
                    material.tableIndex != materialInstanceId)
                    continue;

                if (material.blockOffsetBytes + bytes.size() <= pool.materialParams.cpu.size())
                {
                    if (!bytes.empty())
                    {
                        std::memcpy(
                            pool.materialParams.cpu.data() + material.blockOffsetBytes, bytes.data(), bytes.size());
                        uploadMaterialParamBlock(pool.materialParams,
                                                 rd,
                                                 material.blockOffsetBytes,
                                                 static_cast<uint32_t>(bytes.size()));
                    }
                }
                pool.shaderMaterials[material.tableIndex] = makeRuntimeInfo();
                if (!properties)
                    indexCache[keyText] = ShaderMaterialIndexCacheEntry {.contentRevision = contentRevision,
                                                                         .materialIndex   = i};
                return i;
            }

            resource::GpuMaterial material;
            material.model            = resource::GpuMaterialModel::eShaderMaterial;
            material.blockOffsetBytes = pool.materialParams.allocAndUpload(
                rd, bytes.data(), static_cast<uint32_t>(std::max<size_t>(bytes.size(), 16)), 16);
            material.tableIndex = materialInstanceId;
            material.padding    = static_cast<uint32_t>(resolved->variantHash);

            const uint32_t index = static_cast<uint32_t>(pool.materials.size());
            pool.materials.push_back(material);
            pool.shaderMaterials[material.tableIndex] = makeRuntimeInfo();
            pool.materialTableDirty = true;
            pool.uploadMaterialTable(rd);
            gpuResources.markContentDirty();
            if (!properties)
                indexCache[keyText] = ShaderMaterialIndexCacheEntry {.contentRevision = contentRevision,
                                                                     .materialIndex   = index};
            return index;
        }

        [[nodiscard]] std::optional<MaterialParamsPBRMR>
        shaderSurfaceMaterialParamsFromAsset(IAssetService&       assets,
                                             IShaderService*      shaders,
                                             std::string_view     materialUri,
                                             const nlohmann::json* overrides = nullptr)
        {
            auto shaderSource = shaderMaterialSourceFromAsset(assets, materialUri);
            if (!shaderSource)
                return std::nullopt;

            nlohmann::json properties = shaderSource->properties.is_object() ? shaderSource->properties :
                                                                               nlohmann::json::object();
            if (overrides && overrides->is_object())
            {
                for (const auto& [key, value] : overrides->items())
                    properties[key] = value;
            }

            MaterialParamsPBRMR params;
            params.metallicFactor = 0.0f;
            params.roughnessFactor = 1.0f;

            auto schema = resolveShaderMaterialSchema(shaders, shaderSource->source);
            glm::vec3 emissiveColor {0.0f};
            float     emissiveStrength {1.0f};
            const auto applySchemaProperty = [&](const material::MaterialPropertySchema& property) {
                switch (property.type)
                {
                    case material::MaterialPropertyType::eVec3:
                    case material::MaterialPropertyType::eVec4:
                    case material::MaterialPropertyType::eColor:
                        if (nameMatchesAny(property.name,
                                           {"basecolor",
                                            "basecolorfactor",
                                            "color",
                                            "tint",
                                            "albedo",
                                            "diffuse"}))
                        {
                            params.baseColor =
                                jsonVec4(properties.value(property.name, nlohmann::json::array()),
                                         schemaVec4Default(property, params.baseColor));
                        }
                        else if (nameMatchesAny(property.name, {"emissive", "emissivecolor", "emissivefactor"}))
                        {
                            emissiveColor = glm::vec3(
                                jsonVec4(properties.value(property.name, nlohmann::json::array()),
                                         schemaVec4Default(property, glm::vec4(emissiveColor, 1.0f))));
                        }
                        break;
                    case material::MaterialPropertyType::eFloat:
                        if (nameMatchesAny(property.name, {"roughness"}))
                        {
                            params.roughnessFactor =
                                glm::clamp(jsonFloat(properties.value(property.name, nlohmann::json {}),
                                                     schemaFloatDefault(property, params.roughnessFactor)),
                                           0.045f,
                                           1.0f);
                        }
                        else if (nameMatchesAny(property.name, {"metallic", "metalness"}))
                        {
                            params.metallicFactor =
                                glm::clamp(jsonFloat(properties.value(property.name, nlohmann::json {}),
                                                     schemaFloatDefault(property, params.metallicFactor)),
                                           0.0f,
                                           1.0f);
                        }
                        else if (nameMatchesAny(property.name, {"alphacutoff", "cutoff"}))
                        {
                            params.alphaCutoff =
                                glm::clamp(jsonFloat(properties.value(property.name, nlohmann::json {}),
                                                     schemaFloatDefault(property, params.alphaCutoff)),
                                           0.0f,
                                           1.0f);
                        }
                        else if (nameMatchesAny(property.name, {"emissivestrength", "emissiveintensity"}))
                        {
                            emissiveStrength = jsonFloat(properties.value(property.name, nlohmann::json {}),
                                                         schemaFloatDefault(property, emissiveStrength));
                        }
                        break;
                    case material::MaterialPropertyType::eBool:
                        if (nameMatchesAny(property.name, {"doublesided", "double_sided"}))
                            params.doubleSided = properties.value(property.name, false) ? 1u : 0u;
                        break;
                    case material::MaterialPropertyType::eTexture2D:
                    {
                        const auto uri = textureUriProperty(properties, property.name);
                        if (uri.empty())
                            break;
                        auto texture = assets.loadTextureAsync(uri);
                        if (!texture.ready())
                            break;

                        if (nameMatchesAny(property.name,
                                           {"basecolortexture",
                                            "basecolortex",
                                            "albedotexture",
                                            "albedotex",
                                            "diffusetexture",
                                            "diffusetex",
                                            "colortexture",
                                            "colortex",
                                            "texture"}))
                        {
                            params.baseColorTex = texture.gpuIndex();
                        }
                        else if (nameMatchesAny(property.name, {"normaltexture", "normaltex"}))
                        {
                            params.normalTex = texture.gpuIndex();
                        }
                        else if (nameMatchesAny(property.name, {"metallicroughnesstexture", "mrtexture", "mrtex"}))
                        {
                            params.mrTex         = texture.gpuIndex();
                            params.mrTextureMode = 0u;
                        }
                        else if (nameMatchesAny(property.name, {"metallictexture", "metallictex", "metalnesstex"}))
                        {
                            params.metallicTex = texture.gpuIndex();
                        }
                        else if (nameMatchesAny(property.name, {"roughnesstexture", "roughnesstex"}))
                        {
                            params.roughnessTex = texture.gpuIndex();
                        }
                        else if (nameMatchesAny(property.name, {"occlusiontexture", "occlusiontex", "aotex"}))
                        {
                            params.occlusionTex = texture.gpuIndex();
                        }
                        else if (nameMatchesAny(property.name, {"emissivetexture", "emissivetex"}))
                        {
                            params.emissiveTex = texture.gpuIndex();
                        }
                        break;
                    }
                    default:
                        break;
                }
            };

            for (const auto& property : schema.parameters)
                applySchemaProperty(property);

            if (schema.parameters.empty() && properties.is_object())
            {
                const auto baseColorValue =
                    properties.value("baseColor", properties.value("tint", nlohmann::json::array()));
                params.baseColor       = jsonVec4(baseColorValue, params.baseColor);
                params.metallicFactor  = glm::clamp(jsonFloat(properties.value("metallic", params.metallicFactor),
                                                             params.metallicFactor),
                                                   0.0f,
                                                   1.0f);
                params.roughnessFactor = glm::clamp(jsonFloat(properties.value("roughness", params.roughnessFactor),
                                                             params.roughnessFactor),
                                                   0.045f,
                                                   1.0f);
                emissiveColor = glm::vec3(jsonVec4(properties.value("emissiveColor", nlohmann::json::array()),
                                                   glm::vec4(emissiveColor, 1.0f)));
                emissiveStrength = jsonFloat(properties.value("emissiveStrength", emissiveStrength), emissiveStrength);
            }

            params.emissiveFactor = glm::vec4(emissiveColor * emissiveStrength, 1.0f);

            return params;
        }

        [[nodiscard]] uint32_t ensureShaderSurfaceMaterialAssetGpuMaterial(IAssetService&       assets,
                                                                          IShaderService*      shaders,
                                                                          IGpuResourceService& gpuResources,
                                                                          rhi::RenderDevice&   rd,
                                                                          std::string_view     materialUri,
                                                                          const nlohmann::json* overrides = nullptr,
                                                                          std::string_view     materialKey = {})
        {
            if (materialUri.empty())
                return std::numeric_limits<uint32_t>::max();

            auto params = shaderSurfaceMaterialParamsFromAsset(assets, shaders, materialUri, overrides);
            if (!params)
                return std::numeric_limits<uint32_t>::max();

            const auto keyText = materialKey.empty() ? std::string(materialUri) : std::string(materialKey);
            const auto idText  = keyText + "#shader-surface";
            return ensurePbrGpuMaterial(gpuResources, rd, material_graph::stableGraphId(idText), *params);
        }

        [[nodiscard]] uint32_t ensureMaterialAssetGpuMaterial(IAssetService&       assets,
                                                              IShaderService*      shaders,
                                                              IGpuResourceService& gpuResources,
                                                              rhi::RenderDevice&   rd,
                                                              std::string_view     materialUri,
                                                              const float          timeSeconds,
                                                              const nlohmann::json* overrides,
                                                              std::string_view     materialKey)
        {
            if (materialUri.empty())
                return std::numeric_limits<uint32_t>::max();

            const uint32_t builtinMaterial =
                ensureBuiltinMaterialAssetGpuMaterial(assets, gpuResources, rd, materialUri, overrides);
            if (builtinMaterial != std::numeric_limits<uint32_t>::max())
                return builtinMaterial;

            const uint32_t realShaderMaterial =
                ensureShaderMaterialAssetGpuMaterial(assets, shaders, gpuResources, rd, materialUri, overrides, materialKey);
            if (realShaderMaterial != std::numeric_limits<uint32_t>::max())
                return realShaderMaterial;

            const uint32_t shaderMaterial =
                ensureShaderSurfaceMaterialAssetGpuMaterial(
                    assets, shaders, gpuResources, rd, materialUri, overrides, materialKey);
            if (shaderMaterial != std::numeric_limits<uint32_t>::max())
                return shaderMaterial;

            auto graphSource = graphMaterialSourceFromAsset(assets, materialUri);
            if (!graphSource)
                return std::numeric_limits<uint32_t>::max();

            nlohmann::json graphProperties = graphSource->properties.is_object() ? graphSource->properties :
                                                                                   nlohmann::json::object();
            if (overrides && overrides->is_object())
            {
                for (const auto& [key, value] : overrides->items())
                    graphProperties[key] = value;
            }

            // Prefer the graph's compiled per-pixel GLSL (eShaderMaterial) when the
            // editor has cooked a mesh-material fragment for it; otherwise use the
            // parametric (constant) reduction.
            const uint32_t graphShaderMaterial = ensureMaterialGraphShaderMaterial(
                assets,
                shaders,
                gpuResources,
                rd,
                graphSource->graphUri,
                &graphProperties,
                materialKey.empty() ? materialUri : materialKey);
            if (graphShaderMaterial != std::numeric_limits<uint32_t>::max())
                return graphShaderMaterial;

            return ensureMaterialGraphGpuMaterial(assets,
                                                  gpuResources,
                                                  rd,
                                                  graphSource->graphUri,
                                                  timeSeconds,
                                                  &graphProperties,
                                                  materialKey.empty() ? materialUri : materialKey);
        }
    } // namespace rsdetail
} // namespace vultra
