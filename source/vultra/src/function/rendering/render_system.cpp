#include "vultra/function/rendering/render_system.hpp"
#include "vultra/function/material_graph/material_graph_compiler.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/engine/engine_context.hpp"
#include "vultra/core/math/math.hpp"
#include "vultra/core/rhi/backends/webgpu/webgpu_command_buffer_access.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/services/window_service.hpp"
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
#include "vultra/function/services/shader_service.hpp"
#include "vultra/function/services/world_service.hpp"
#include "vultra/function/world/components/entity_status_component.hpp"
#include "vultra/function/world/components/environment_component.hpp"
#include "vultra/function/world/components/gaussian_splat_component.hpp"
#include "vultra/function/world/components/id_component.hpp"
#include "vultra/function/world/components/light_component.hpp"
#include "vultra/function/world/components/mesh_component.hpp"
#include "vultra/function/world/components/reflection_probe_component.hpp"
#include "vultra/function/world/components/transform_component.hpp"
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
#include <bit>
#include <cctype>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <limits>
#include <numeric>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#ifndef NDEBUG
#include <fstream>
#endif

namespace vultra
{
    namespace
    {
        struct alignas(16) MaterialGraphSurfaceParams
        {
            glm::vec4  baseColor {1.0f};
            glm::vec4  emissiveAlpha {0.0f, 0.0f, 0.0f, 1.0f};
            glm::vec4  metallicRoughnessAoCutoff {0.0f, 1.0f, 1.0f, 0.5f};
            glm::uvec4 textureInfo {0u};
            uint32_t   graphId {0};
            uint32_t   alphaMode {0};
            uint32_t   shadingModel {0};
            uint32_t   flags {0};
        };

        static_assert(sizeof(MaterialGraphSurfaceParams) % 16 == 0);

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

        [[nodiscard]] nlohmann::json constantNodeValue(const material_graph::Graph& graph,
                                                       const material_graph::Node&  node,
                                                       std::string_view             outputPin,
                                                       const nlohmann::json&        fallback,
                                                       const float                  timeSeconds)
        {
            const auto inputValue = [&](std::string_view pin, const nlohmann::json& inputFallback) {
                const auto* inputLink = linkedInput(graph, node, pin);
                const auto* inputNode = inputLink ? material_graph::findNode(graph, inputLink->from.nodeId) : nullptr;
                return inputNode ?
                           constantNodeValue(graph, *inputNode, inputLink->from.pin, inputFallback, timeSeconds) :
                           inputFallback;
            };

            if (node.typeId == "vultra.param.float" || node.typeId == "vultra.param.vec2" ||
                node.typeId == "vultra.param.vec3" || node.typeId == "vultra.param.vec4" ||
                node.typeId == "vultra.param.color" || node.typeId == "vultra.param.bool" ||
                node.typeId == "vultra.param.int" || node.typeId == "vultra.param.enum")
                return node.params.value("value", fallback);

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
                                                       const float                  timeSeconds)
        {
            if (const auto* link = linkedInput(graph, output, pin))
            {
                if (const auto* source = material_graph::findNode(graph, link->from.nodeId))
                    return constantNodeValue(graph, *source, link->from.pin, fallback, timeSeconds);
            }
            return output.params.value(std::string(pin), fallback);
        }

        [[nodiscard]] uint32_t materialGraphTextureIndex(IAssetService&               assets,
                                                         const material_graph::Graph& graph,
                                                         const material_graph::Node&  output,
                                                         std::string_view             pin)
        {
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
                    const auto uri = node.params.value("texture", std::string {});
                    if (uri.empty())
                        return 0u;

                    auto texture = assets.loadTextureSync(uri);
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

        [[nodiscard]] MaterialGraphSurfaceParams materialGraphSurfaceParams(IAssetService&   assets,
                                                                            std::string_view materialGraphUri,
                                                                            const uint32_t   graphId,
                                                                            const float      timeSeconds)
        {
            MaterialGraphSurfaceParams params {};
            params.graphId = graphId;

            std::vector<material_graph::Diagnostic> diagnostics;
            auto                                    text = assets.loadTextAssetSync(materialGraphUri);
            if (!text)
                return params;

            auto graph = material_graph::loadGraphFromText(text.value(), &diagnostics);
            if (!graph)
                return params;

            const auto output =
                std::find_if(graph->nodes.begin(), graph->nodes.end(), [](const material_graph::Node& node) {
                    return node.typeId == "vultra.output.surface";
                });
            if (output == graph->nodes.end())
                return params;

            params.baseColor = jsonVec4(
                surfaceInputValue(
                    *graph, *output, "baseColor", nlohmann::json::array({1.0f, 1.0f, 1.0f, 1.0f}), timeSeconds),
                glm::vec4(1.0f));
            const glm::vec3 emissive = jsonVec3(
                surfaceInputValue(*graph, *output, "emissive", nlohmann::json::array({0.0f, 0.0f, 0.0f}), timeSeconds),
                glm::vec3(0.0f));
            params.emissiveAlpha = glm::vec4(
                emissive,
                glm::clamp(
                    jsonFloat(surfaceInputValue(*graph, *output, "alpha", 1.0f, timeSeconds), 1.0f), 0.0f, 1.0f));
            params.metallicRoughnessAoCutoff = glm::vec4(
                glm::clamp(
                    jsonFloat(surfaceInputValue(*graph, *output, "metallic", 0.0f, timeSeconds), 0.0f), 0.0f, 1.0f),
                glm::clamp(
                    jsonFloat(surfaceInputValue(*graph, *output, "roughness", 1.0f, timeSeconds), 1.0f), 0.045f, 1.0f),
                glm::clamp(jsonFloat(surfaceInputValue(*graph, *output, "ao", 1.0f, timeSeconds), 1.0f), 0.0f, 1.0f),
                glm::clamp(
                    jsonFloat(surfaceInputValue(*graph, *output, "alphaCutoff", 0.5f, timeSeconds), 0.5f), 0.0f, 1.0f));
            params.textureInfo.x = materialGraphTextureIndex(assets, *graph, *output, "baseColor");
            params.alphaMode     = static_cast<uint32_t>(
                material_graph::alphaModeFromString(output->params.value("alphaMode", std::string {"Opaque"})));
            params.shadingModel = static_cast<uint32_t>(
                material_graph::shadingModelFromString(output->params.value("shadingModel", std::string {"PBR_MR"})));
            if (params.shadingModel == static_cast<uint32_t>(material_graph::ShadingModel::ePBRSpecularGlossiness) ||
                params.shadingModel == static_cast<uint32_t>(material_graph::ShadingModel::ePhong))
                params.metallicRoughnessAoCutoff.x = 0.0f;
            return params;
        }

        void uploadMaterialGraphParams(resource::GpuResourcePool&        pool,
                                       rhi::RenderDevice&                rd,
                                       resource::GpuMaterial&            material,
                                       const MaterialGraphSurfaceParams& params)
        {
            if (material.blockOffsetBytes + sizeof(params) <= pool.materialParams.cpu.size())
            {
                std::memcpy(pool.materialParams.cpu.data() + material.blockOffsetBytes, &params, sizeof(params));
                if (pool.materialParams.gpu)
                    rd.uploadS(*pool.materialParams.gpu,
                               0,
                               static_cast<uint64_t>(pool.materialParams.cpu.size()),
                               pool.materialParams.cpu.data());
            }
            else
            {
                material.blockOffsetBytes = pool.materialParams.allocAndUpload(rd, &params, sizeof(params));
                pool.materialTableDirty   = true;
            }
        }

        [[nodiscard]] uint32_t ensureMaterialGraphGpuMaterial(IAssetService&       assets,
                                                              IGpuResourceService& gpuResources,
                                                              rhi::RenderDevice&   rd,
                                                              std::string_view     materialGraphUri,
                                                              const float          timeSeconds)
        {
            if (materialGraphUri.empty())
                return std::numeric_limits<uint32_t>::max();

            auto&          pool    = gpuResources.pool();
            const uint32_t graphId = material_graph::stableGraphId(materialGraphUri);
            const auto     params  = materialGraphSurfaceParams(assets, materialGraphUri, graphId, timeSeconds);
            for (uint32_t i = 0; i < static_cast<uint32_t>(pool.materials.size()); ++i)
            {
                auto& material = pool.materials[i];
                if (material.model == resource::GpuMaterialModel::eMaterialGraph && material.tableIndex == graphId)
                {
                    uploadMaterialGraphParams(pool, rd, material, params);
                    return i;
                }
            }

            resource::GpuMaterial material;
            material.model            = resource::GpuMaterialModel::eMaterialGraph;
            material.blockOffsetBytes = pool.materialParams.allocAndUpload(rd, &params, sizeof(params));
            material.tableIndex       = graphId;
            material.padding          = 0u;

            const uint32_t index = static_cast<uint32_t>(pool.materials.size());
            pool.materials.push_back(material);
            pool.materialTableDirty = true;
            gpuResources.markContentDirty();
            return index;
        }

        [[nodiscard]] uint32_t
        remapMaterialIndex(const RenderInstance& instance, const resource::GpuMesh& mesh, const uint32_t materialIndex)
        {
            if (materialIndex < mesh.materialOffset)
                return materialIndex;
            const uint32_t localSlot = materialIndex - mesh.materialOffset;
            if (localSlot >= mesh.materialCount)
                return materialIndex;
            for (const auto& override : instance.materialOverrides)
            {
                if (override.slot == localSlot)
                    return override.materialIndex;
            }
            return materialIndex;
        }

        [[nodiscard]] bool isEntityRenderable(const World& world, const entt::registry& reg, entt::entity entity)
        {
            for (auto e = entity; e != entt::null; e = world.parent(e))
            {
                if (!reg.valid(e))
                    return false;
                if (auto* status = reg.try_get<EntityStatusComponent>(e);
                    status && (!status->active || !status->visible))
                    return false;
            }
            return true;
        }

        void buildCpuDrivenGpuSceneForRenderWorld(RenderWorld&                     renderWorld,
                                                  resource::GpuSceneDatabase&      gpuSceneDatabase,
                                                  resource::GpuSceneView&          gpuSceneView,
                                                  const resource::GpuResourcePool& pool,
                                                  rhi::RenderDevice&               rd,
                                                  rhi::CommandBuffer&              cb)
        {
            gpuSceneDatabase.beginFrame(pool);
            gpuSceneDatabase.instances.reserve(renderWorld.instances.size());
            gpuSceneDatabase.transforms.reserve(renderWorld.instances.size());
            gpuSceneDatabase.rebuildMeshTableFromResources();

            for (const auto& inst : renderWorld.instances)
            {
                const uint32_t        transformIndex = gpuSceneDatabase.pushTransform(inst.worldMatrix);
                resource::GpuInstance gpuInst {};
                gpuInst.meshIndex      = inst.meshIndex;
                gpuInst.materialIndex  = inst.materialIndex;
                gpuInst.transformIndex = transformIndex;
                gpuInst.flags          = 0;
                gpuSceneDatabase.pushInstance(gpuInst);
            }
            gpuSceneDatabase.uploadSceneTables(rd, cb);

            uint32_t maxMeshletDraws = 0;
            for (const auto& inst : renderWorld.instances)
            {
                if (inst.meshIndex >= pool.meshes.size())
                    continue;
                maxMeshletDraws += pool.meshes[inst.meshIndex].meshletCount;
            }

            gpuSceneView.beginFrame(gpuSceneDatabase, resource::GpuSceneBuildMode::eCpuDriven);
            gpuSceneView.setGpuDrivenCaps(
                static_cast<uint32_t>(gpuSceneDatabase.instances.size()), maxMeshletDraws, maxMeshletDraws);
            gpuSceneView.ensureVisibleMeshletBuffers(rd);
            gpuSceneView.draws.reserve(maxMeshletDraws);

            for (uint32_t instanceIndex = 0; instanceIndex < static_cast<uint32_t>(renderWorld.instances.size());
                 ++instanceIndex)
            {
                const auto& inst = renderWorld.instances[instanceIndex];
                if (inst.meshIndex >= pool.meshes.size() || instanceIndex >= gpuSceneDatabase.instances.size())
                    continue;

                const auto& mesh = pool.meshes[inst.meshIndex];
                for (uint32_t localMeshlet = 0; localMeshlet < mesh.meshletCount; ++localMeshlet)
                {
                    const uint32_t globalMeshletIndex = mesh.meshletOffset + localMeshlet;
                    if (globalMeshletIndex >= pool.meshlets.cpuMeshlets.size())
                        continue;

                    const auto&             meshlet = pool.meshlets.cpuMeshlets[globalMeshletIndex];
                    resource::GpuDrawRecord dr {};
                    dr.primitiveIndex       = globalMeshletIndex;
                    dr.materialIndex        = remapMaterialIndex(inst, mesh, meshlet.materialIndex);
                    dr.vertexStrideBytes    = mesh.vertexStrideBytes;
                    dr.flags                = resource::gpuDrawFlagsToMask(resource::GpuDrawFlags::eMeshlet);
                    dr.vertexAddress        = pool.geometry.vertexBytesAddress;
                    dr.instanceIndex        = instanceIndex;
                    const auto layout       = resource::inspectGpuVertexLayout(mesh.vertexAttributes);
                    dr.vertexAttributeMask  = layout.attributeMask;
                    dr.positionOffsetBytes  = layout.positionOffsetBytes;
                    dr.normalOffsetBytes    = layout.normalOffsetBytes;
                    dr.colorOffsetBytes     = layout.colorOffsetBytes;
                    dr.texCoord0OffsetBytes = layout.texCoord0OffsetBytes;
                    dr.texCoord1OffsetBytes = layout.texCoord1OffsetBytes;
                    dr.tangentOffsetBytes   = layout.tangentOffsetBytes;
                    dr.model                = inst.worldMatrix;
                    gpuSceneView.pushMeshletDraw(std::move(dr));
                }
            }

            std::stable_sort(gpuSceneView.draws.begin(), gpuSceneView.draws.end(), [](const auto& a, const auto& b) {
                if (a.materialIndex != b.materialIndex)
                    return a.materialIndex < b.materialIndex;
                return a.primitiveIndex < b.primitiveIndex;
            });

            gpuSceneView.uploadDraws(rd, cb);
            gpuSceneView.buildIndirectFromDraws(pool);
            gpuSceneView.uploadIndirect(rd);

            renderWorld.gpuSceneDatabase = &gpuSceneDatabase;
            renderWorld.gpuSceneView     = &gpuSceneView;
        }
    } // namespace

    namespace
    {
        thread_local rhi::BuiltinProfilerGpuScopeContext g_CurrentBuiltinProfilerGpuScopeContext {};

        [[nodiscard]] constexpr bool isTrackyGpuProfilerEnabled()
        {
#if defined(TRACKY_ENABLE) && TRACKY_ENABLE
            return true;
#else
            return false;
#endif
        }

        [[nodiscard]] std::string rendererKeyFromRenderGraphUri(std::string_view uri)
        {
            auto filename = std::filesystem::path(std::string(uri)).filename().generic_string();
            constexpr std::array<std::string_view, 2> suffixes {".vrg.json", ".vrp.lua"};
            for (const auto suffix : suffixes)
            {
                if (filename.ends_with(suffix))
                {
                    filename.resize(filename.size() - suffix.size());
                    break;
                }
            }
            if (filename.empty())
                filename = "custom";
            for (auto& ch : filename)
            {
                if (ch == '-' || ch == ' ')
                    ch = '_';
                else
                    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            }
            return filename;
        }

        [[nodiscard]] bool rendererKeyRequiresRayTracingScene(std::string_view rendererKey)
        {
            return rendererKey == "universal_rt" || rendererKey == "default_rt";
        }

        void expandBounds(RenderWorld& out, const glm::vec3& point)
        {
            if (!out.hasBounds)
            {
                out.hasBounds = true;
                out.boundsMin = point;
                out.boundsMax = point;
                return;
            }
            out.boundsMin = glm::min(out.boundsMin, point);
            out.boundsMax = glm::max(out.boundsMax, point);
        }

        void expandBounds(RenderWorld& out, const glm::mat4& model, const glm::vec3& center, const float radius)
        {
            const glm::vec3 worldCenter = glm::vec3(model * glm::vec4(center, 1.0f));
            const float     maxScale    = std::max({
                glm::length(glm::vec3(model[0])),
                glm::length(glm::vec3(model[1])),
                glm::length(glm::vec3(model[2])),
            });
            const glm::vec3 r(std::max(radius, 0.0f) * maxScale);
            expandBounds(out, worldCenter - r);
            expandBounds(out, worldCenter + r);
        }

        struct FrameGraphSnapshotWriter
        {
            nlohmann::json                          snapshot;
            std::unordered_map<std::string, size_t> emittedNodes;
            std::unordered_set<std::string>         emittedEdges;

            FrameGraphSnapshotWriter(std::string_view cameraName, std::string_view rendererKey, std::string dot)
            {
                snapshot["camera"]   = cameraName;
                snapshot["renderer"] = rendererKey;
                snapshot["dot"]      = std::move(dot);
                snapshot["nodes"]    = nlohmann::json::array();
                snapshot["edges"]    = nlohmann::json::array();
            }

            static std::string passId(const PassNode& pass) { return "pass:" + std::to_string(pass.getId()); }

            static std::string resourceId(const ResourceNode& resource)
            {
                return "resource:" + std::to_string(resource.getResourceId()) + "_v" +
                       std::to_string(resource.getVersion());
            }

            static const ResourceNode* findResource(const std::vector<ResourceNode>& resources, FrameGraphResource id)
            {
                const auto it = std::find_if(
                    resources.begin(), resources.end(), [&](const auto& resource) { return resource.getId() == id; });
                return it != resources.end() ? &*it : nullptr;
            }

            void emitNode(std::string id, std::string label, std::string kind, nlohmann::json extra = {})
            {
                if (id.empty())
                    return;

                if (auto it = emittedNodes.find(id); it != emittedNodes.end())
                {
                    auto& node = snapshot["nodes"][it->second];
                    if (!label.empty())
                        node["label"] = std::move(label);
                    if (!kind.empty())
                        node["kind"] = std::move(kind);
                    node.update(extra);
                    return;
                }

                nlohmann::json node {
                    {"id", id},
                    {"label", std::move(label)},
                    {"kind", std::move(kind)},
                };
                node.update(extra);
                emittedNodes.emplace(std::move(id), snapshot["nodes"].size());
                snapshot["nodes"].push_back(std::move(node));
            }

            void emitPass(const PassNode& pass)
            {
                emitNode(passId(pass),
                         std::string(pass.getName()),
                         "pass",
                         nlohmann::json {
                             {"sideEffect", pass.hasSideEffect()},
                             {"active", pass.canExecute()},
                         });
            }

            void emitResource(const ResourceNode& resource, std::optional<bool> imported = std::nullopt)
            {
                std::string label(resource.getName());
                if (resource.getVersion() > ResourceEntry::kInitialVersion)
                    label += " v" + std::to_string(resource.getVersion());

                nlohmann::json extra {
                    {"version", resource.getVersion()},
                    {"refCount", resource.getRefCount()},
                };
                if (imported)
                    extra["imported"] = *imported;

                emitNode(resourceId(resource), std::move(label), "resource", std::move(extra));
            }

            void emitEdge(std::string from, std::string to, std::string label)
            {
                if (from.empty() || to.empty() || from == to)
                    return;

                const std::string key = from + "->" + to + ":" + label;
                if (!emittedEdges.insert(key).second)
                    return;

                snapshot["edges"].push_back({
                    {"from", std::move(from)},
                    {"to", std::move(to)},
                    {"label", std::move(label)},
                });
            }

            void operator()(const PassNode& pass, const std::vector<ResourceNode>& resources)
            {
                if (!pass.canExecute())
                    return;

                emitPass(pass);
                const auto pid = passId(pass);

                for (const auto& access : pass.each(PassNode::Read {}))
                {
                    if (const auto* resource = findResource(resources, access.id))
                    {
                        emitResource(*resource);
                        emitEdge(resourceId(*resource), pid, "read");
                    }
                }
                for (const auto& access : pass.each(PassNode::Write {}))
                {
                    if (const auto* resource = findResource(resources, access.id))
                    {
                        emitResource(*resource);
                        emitEdge(pid, resourceId(*resource), "write");
                    }
                }
            }

            void operator()(const ResourceNode& resource, const ResourceEntry& entry, const std::vector<PassNode>&)
            {
                if (resource.getRefCount() > 0)
                    emitResource(resource, entry.isImported());
            }

            void flush(std::ostream& os) const { os << snapshot.dump(); }
        };

        void clearColorTarget(rhi::CommandBuffer&                   cb,
                              rhi::Texture&                         target,
                              const rhi::Rect2D&                    area,
                              const std::optional<rhi::ClearValue>& clearValue,
                              const bool                            enableMultiview,
                              const uint32_t                        multiviewMask)
        {
            rhi::FramebufferInfo clearFbInfo {
                .area             = area,
                .layers           = enableMultiview ? 2u : 1u,
                .viewMask         = enableMultiview ? multiviewMask : 0u,
                .colorAttachments = {rhi::AttachmentInfo {
                    .target = &target,
                    .clearValue =
                        clearValue.has_value() ? clearValue : std::optional<rhi::ClearValue> {glm::vec4 {0, 0, 0, 1}},
                }},
            };

            rhi::prepareForAttachment(cb, target, false);
            cb.beginRendering(clearFbInfo);
            cb.endRendering();
        }

        [[nodiscard]] glm::vec3 safeNormalizeDirection(const glm::vec3& direction, const glm::vec3& fallback)
        {
            const float len2 = glm::dot(direction, direction);
            return len2 > 1e-8f ? direction * glm::inversesqrt(len2) : fallback;
        }

        void finalizeRenderCamera(RenderCamera& cam)
        {
            cam.viewProjection        = cam.projection * cam.view;
            cam.inverseView           = glm::inverse(cam.view);
            cam.inverseProjection     = glm::inverse(cam.projection);
            cam.inverseViewProjection = glm::inverse(cam.viewProjection);

            auto planes = math::extractFrustumPlanes(cam.viewProjection);
            for (int i = 0; i < 6; ++i)
                cam.frustumPlanes[i] = glm::vec4(planes[i].normal, planes[i].d);
        }

        [[nodiscard]] RenderCamera cameraForRenderExtent(const RenderCamera& src, const rhi::Extent2D extent)
        {
            RenderCamera cam = src;
            if (cam.isXRView)
                return cam;

            const float aspect =
                static_cast<float>(std::max(extent.width, 1u)) / static_cast<float>(std::max(extent.height, 1u));
            if (std::abs(cam.projection[3][3]) < 1e-5f)
            {
                cam.projection = glm::perspectiveRH_ZO(cam.fovY, std::max(aspect, 0.0001f), cam.zNear, cam.zFar);
                finalizeRenderCamera(cam);
            }
            else
            {
                const float orthoHeight = cam.projection[1][1] != 0.0f ? std::abs(2.0f / cam.projection[1][1]) : 1.0f;
                const float orthoWidth  = orthoHeight * std::max(aspect, 0.0001f);
                cam.projection          = glm::orthoRH_ZO(-orthoWidth * 0.5f,
                                                 orthoWidth * 0.5f,
                                                 -orthoHeight * 0.5f,
                                                 orthoHeight * 0.5f,
                                                 cam.zNear,
                                                 cam.zFar);
                finalizeRenderCamera(cam);
            }
            return cam;
        }

        [[nodiscard]] glm::vec3 lightDirectionFromTransformNormal(const TransformComponent& transform)
        {
            return safeNormalizeDirection(-glm::vec3(transform.worldMatrix[2]), glm::vec3 {0.0f, -1.0f, 0.0f});
        }

        [[nodiscard]] glm::mat4 areaLightSurfaceMatrix(const RenderLight& light)
        {
            const glm::vec3 normal = safeNormalizeDirection(light.direction, glm::vec3 {0.0f, -1.0f, 0.0f});
            glm::vec3       up {0.0f, 1.0f, 0.0f};
            if (std::abs(glm::dot(up, normal)) > 0.95f)
                up = glm::vec3 {1.0f, 0.0f, 0.0f};

            const glm::vec3 tangent =
                safeNormalizeDirection(glm::cross(up, normal), glm::vec3 {1.0f, 0.0f, 0.0f});
            const glm::vec3 bitangent =
                safeNormalizeDirection(glm::cross(normal, tangent), glm::vec3 {0.0f, 1.0f, 0.0f});

            glm::mat4 out {1.0f};
            out[0] = glm::vec4(tangent * std::max(light.width, 0.001f), 0.0f);
            out[1] = glm::vec4(-normal, 0.0f);
            out[2] = glm::vec4(bitangent * std::max(light.height, 0.001f), 0.0f);
            out[3] = glm::vec4(light.position, 1.0f);
            return out;
        }

        float effectiveGaussianAutomaticClodLevel(const GaussianSplatRenderSettings& settings)
        {
            if (!settings.foveatedClodActive())
                return std::clamp(settings.clodLevel, 0.01f, 1.0f);

            const glm::vec3 levels {
                std::clamp(settings.foveatedRingLevels.x, 0.0f, 1.0f),
                std::clamp(settings.foveatedRingLevels.y, 0.0f, 1.0f),
                std::clamp(settings.foveatedRingLevels.z, 0.0f, 1.0f),
            };
            return std::clamp(std::max(levels.x, std::max(levels.y, levels.z)), 0.01f, 1.0f);
        }

        uint32_t effectiveGaussianLodBudget(const GaussianSplatRenderSettings& settings, const uint32_t totalSplatCount)
        {
            // Baseline consumes the full table. Ordered CLOD consumes a prefix of
            // the table that vasset already sorted by importance at import time.
            if (!settings.lodBudgetEnabled())
                return totalSplatCount;

            // An explicit budget is useful for repeatable profiling. With budget 0
            // the UI exposes clodLevel as the paper-style continuous LOD fraction.
            if (settings.lodBudget > 0u)
                return std::min(totalSplatCount, settings.lodBudget);
            const float clodLevel = effectiveGaussianAutomaticClodLevel(settings);
            return std::min(
                totalSplatCount,
                std::max(1u, static_cast<uint32_t>(std::ceil(static_cast<float>(totalSplatCount) * clodLevel))));
        }

        void applyGaussianSplatFoveatedClodSettings(resource::GpuSceneView&            gpuSceneView,
                                                    const GaussianSplatRenderSettings& settings)
        {
            const auto layers = settings.foveatedLayers();
            gpuSceneView.setGeneralGaussianSplatFoveatedClod(
                settings.foveatedClodActive(),
                settings.foveatedLayeredCompositeActive(),
                settings.foveatedGaze,
                glm::vec2 {layers[0].eccentricityDegrees, layers[1].eccentricityDegrees},
                glm::vec3 {layers[0].lodLevel, layers[1].lodLevel, layers[2].lodLevel},
                glm::vec3 {layers[0].resolutionScale, layers[1].resolutionScale, layers[2].resolutionScale},
                std::max(settings.foveatedTransitionDegrees, 0.0f));
        }

        void resetGaussianSplatIndirectBuffer(rhi::RenderDevice& rd, rhi::DrawIndirectBuffer& buffer)
        {
            std::vector<rhi::DrawIndirectCommand> indirect(1u);
            indirect[0].type          = rhi::DrawIndirectType::eNonIndexed;
            indirect[0].count         = 4u;
            indirect[0].instanceCount = 0u;
            indirect[0].first         = 0u;
            indirect[0].vertexOffset  = 0;
            indirect[0].firstInstance = 0u;
            rd.uploadDrawIndirect(buffer, indirect);
        }

        void resetGaussianSplatIndirectBuffers(rhi::RenderDevice& rd, resource::GpuSceneView& gpuSceneView)
        {
            if (gpuSceneView.generalGaussianSplatIndirectBuffer.has_value())
                resetGaussianSplatIndirectBuffer(rd, gpuSceneView.generalGaussianSplatIndirectBuffer.value());

            for (auto& buffer : gpuSceneView.generalGaussianSplatFoveatedIndirectBuffers)
            {
                if (buffer.has_value())
                    resetGaussianSplatIndirectBuffer(rd, buffer.value());
            }
        }

        bool gaussianSplatSelectionSettingsDirty(const GaussianSplatRenderSettings& current,
                                                 const GaussianSplatRenderSettings& applied)
        {
            return current.lodBudget != applied.lodBudget || current.clodLevel != applied.clodLevel ||
                   current.foveatedClodEnabled != applied.foveatedClodEnabled ||
                   current.foveatedRenderMode != applied.foveatedRenderMode ||
                   current.foveatedGaze != applied.foveatedGaze ||
                   current.foveatedRingDegrees != applied.foveatedRingDegrees ||
                   current.foveatedRingLevels != applied.foveatedRingLevels ||
                   current.foveatedResolutionScales != applied.foveatedResolutionScales ||
                   current.foveatedTransitionDegrees != applied.foveatedTransitionDegrees;
        }

        void updateGaussianSplatFoveatedBudgetController(GaussianSplatRenderSettings& settings, const double gpuFrameMs)
        {
            if (!settings.foveatedClodActive() || !settings.foveatedBudgetControllerEnabled || gpuFrameMs <= 0.0)
                return;

            const float targetMs = std::max(settings.foveatedTargetFrameMs, 0.1f);
            const float maxStep  = std::clamp(settings.foveatedBudgetAdjustRate, 0.001f, 0.25f);
            const float error    = static_cast<float>((targetMs - gpuFrameMs) / targetMs);
            if (std::abs(error) < 0.03f)
                return;

            const float signedStep = std::clamp(error * 0.5f, -maxStep, maxStep);
            auto        adjust     = [signedStep](float value, const float floorValue) {
                return std::clamp(value + signedStep * std::max(value, 0.1f), floorValue, 1.0f);
            };

            settings.foveatedRingLevels.z = adjust(settings.foveatedRingLevels.z, 0.01f);
            settings.foveatedRingLevels.y = adjust(settings.foveatedRingLevels.y, settings.foveatedRingLevels.z);
            if (error < -0.35f)
                settings.foveatedRingLevels.x = adjust(settings.foveatedRingLevels.x, settings.foveatedRingLevels.y);
            else
                settings.foveatedRingLevels.x = std::max(settings.foveatedRingLevels.x, settings.foveatedRingLevels.y);
        }

        void
        rebuildGaussianSplatOrderedClodPrefixSources(resource::GpuSceneView&                         gpuSceneView,
                                                     const std::vector<RenderGaussianSplatInstance>& gaussianSplats,
                                                     const resource::GpuResourcePool&                pool,
                                                     RuntimeProfiler&                                profiler)
        {
            gpuSceneView.generalGaussianSplatSelectedSources.clear();

            struct OrderedSource
            {
                uint32_t                                        rankNumerator {0};
                uint32_t                                        pointCount {1};
                uint32_t                                        drawIndex {0};
                uint32_t                                        rank {0};
                resource::GpuGeneralGaussianSplatSelectedSource selection {};
            };

            RuntimeProfiler::Scope     scope {profiler, "GaussianCLOD::BuildPrefix"};
            const bool                 singleDraw = gpuSceneView.generalGaussianSplatDraws.size() <= 1u;
            std::vector<OrderedSource> orderedSources;
            if (!singleDraw)
            {
                uint32_t reserveCount = 0u;
                for (const auto& splatInst : gaussianSplats)
                {
                    if (splatInst.splatIndex < pool.gaussianSplats.size())
                        reserveCount += pool.gaussianSplats[splatInst.splatIndex].pointCount;
                }
                orderedSources.reserve(reserveCount);
            }

            uint32_t drawIndex = 0u;
            for (const auto& splatInst : gaussianSplats)
            {
                if (splatInst.splatIndex >= pool.gaussianSplats.size())
                    continue;
                if (drawIndex >= gpuSceneView.generalGaussianSplatDraws.size())
                    break;

                const auto& gpuSplat = pool.gaussianSplats[splatInst.splatIndex];
                if (gpuSplat.pointCount == 0u)
                    continue;

                const auto&    drawRecord   = gpuSceneView.generalGaussianSplatDraws[drawIndex];
                const uint32_t sourceOffset = drawRecord.pointOffset;
                const uint32_t rankCount    = gpuSplat.pointCount;

                for (uint32_t rank = 0u; rank < rankCount; ++rank)
                {
                    const uint32_t localPoint = rank;

                    resource::GpuGeneralGaussianSplatSelectedSource selection {};
                    selection.sourceIndex  = sourceOffset + localPoint;
                    selection.drawIndex    = drawIndex;
                    selection.packedWeight = std::bit_cast<uint32_t>(1.0f);
                    selection.flags        = 0u;

                    if (singleDraw)
                    {
                        gpuSceneView.pushGeneralGaussianSplatSelectedSource(selection);
                    }
                    else
                    {
                        orderedSources.push_back(OrderedSource {
                            .rankNumerator = rank + 1u,
                            .pointCount    = gpuSplat.pointCount,
                            .drawIndex     = drawIndex,
                            .rank          = rank,
                            .selection     = selection,
                        });
                    }
                }

                ++drawIndex;
            }

            if (!singleDraw)
            {
                std::stable_sort(orderedSources.begin(), orderedSources.end(), [](const auto& a, const auto& b) {
                    const uint64_t lhs = static_cast<uint64_t>(a.rankNumerator) * static_cast<uint64_t>(b.pointCount);
                    const uint64_t rhs = static_cast<uint64_t>(b.rankNumerator) * static_cast<uint64_t>(a.pointCount);
                    if (lhs != rhs)
                        return lhs < rhs;
                    if (a.drawIndex != b.drawIndex)
                        return a.drawIndex < b.drawIndex;
                    return a.rank < b.rank;
                });

                gpuSceneView.generalGaussianSplatSelectedSources.reserve(orderedSources.size());
                for (const auto& source : orderedSources)
                    gpuSceneView.pushGeneralGaussianSplatSelectedSource(source.selection);
            }
        }

        void rebuildGaussianSplatSelectedSources(resource::GpuSceneView&                         gpuSceneView,
                                                 const std::vector<RenderGaussianSplatInstance>& gaussianSplats,
                                                 const resource::GpuResourcePool&                pool,
                                                 GaussianSplatFrameStats&                        stats,
                                                 RuntimeProfiler&                                profiler)
        {
            gpuSceneView.generalGaussianSplatSelectedSources.clear();

            // Baseline still builds a selected-source table so the preprocess
            // shader can share one path with Ordered CLOD.
            RuntimeProfiler::Scope scope {profiler, "GaussianSplat::BuildRawSelection"};
            uint32_t               drawIndex = 0u;
            for (const auto& splatInst : gaussianSplats)
            {
                if (splatInst.splatIndex >= pool.gaussianSplats.size())
                    continue;
                if (drawIndex >= gpuSceneView.generalGaussianSplatDraws.size())
                    break;

                const auto& gpuSplat = pool.gaussianSplats[splatInst.splatIndex];
                if (gpuSplat.pointCount == 0u)
                    continue;

                const auto&    drawRecord   = gpuSceneView.generalGaussianSplatDraws[drawIndex];
                const uint32_t sourceOffset = drawRecord.pointOffset;
                for (uint32_t localPoint = 0u; localPoint < gpuSplat.pointCount; ++localPoint)
                {
                    resource::GpuGeneralGaussianSplatSelectedSource selection {};
                    selection.sourceIndex  = sourceOffset + localPoint;
                    selection.drawIndex    = drawIndex;
                    selection.packedWeight = std::bit_cast<uint32_t>(1.0f);
                    selection.flags        = 0u;
                    gpuSceneView.pushGeneralGaussianSplatSelectedSource(selection);
                    ++stats.lodSelectedRawSplats;
                }

                ++drawIndex;
            }
        }
    } // namespace

    void RenderWorldCooker::cook(World&               world,
                                 IAssetService&       assets,
                                 IGpuResourceService& gpuResources,
                                 rhi::RenderDevice&   rd,
                                 GeometryFactory&     geometryFactory,
                                 RenderWorld&         out,
                                 const float          timeSeconds)
    {
        out.clear();

        auto& reg = world.registry();

        auto view = reg.view<IDComponent, TransformComponent, MeshComponent>();
        out.instances.reserve(view.size_hint());
        for (auto e : view)
        {
            const auto& id   = view.get<IDComponent>(e);
            const auto& tr   = view.get<TransformComponent>(e);
            const auto& mesh = view.get<MeshComponent>(e);
            if (!isEntityRenderable(world, reg, e))
                continue;

            uint32_t meshIndex = std::numeric_limits<uint32_t>::max();
            if (mesh.builtinGeometry != UINT32_MAX)
            {
                meshIndex = geometryFactory.getOrCreateMeshIndex(
                    static_cast<BuiltinGeometryKind>(mesh.builtinGeometry), gpuResources, rd);
            }
            else
            {
                auto h = assets.loadMeshSync(mesh.mesh);
                if (!h.ready())
                    continue;
                meshIndex = h.gpuIndex();
            }

            if (meshIndex == std::numeric_limits<uint32_t>::max())
                continue;

            RenderInstance inst {};
            inst.entity      = id.uuid;
            inst.meshIndex   = meshIndex;
            inst.worldMatrix = tr.worldMatrix;
            inst.materialOverrides.reserve(mesh.materialOverrides.size());
            for (const auto& materialOverride : mesh.materialOverrides)
            {
                const uint32_t graphMaterialIndex = ensureMaterialGraphGpuMaterial(
                    assets, gpuResources, rd, materialOverride.materialGraph, timeSeconds);
                if (graphMaterialIndex != std::numeric_limits<uint32_t>::max())
                {
                    inst.materialOverrides.push_back(RenderInstance::MaterialOverride {
                        .slot          = materialOverride.slot,
                        .materialIndex = graphMaterialIndex,
                    });
                }
            }
            if (mesh.builtinGeometry != UINT32_MAX && inst.materialOverrides.empty())
            {
                inst.baseColorOverride    = mesh.materialColor;
                inst.hasBaseColorOverride = true;
            }
            out.instances.push_back(inst);

            const auto& pool = gpuResources.pool();
            if (meshIndex < pool.meshes.size())
            {
                const auto& gpuMesh = pool.meshes[meshIndex];
                if (gpuMesh.meshletCount > 0 &&
                    gpuMesh.meshletOffset + gpuMesh.meshletCount <= pool.meshlets.cpuMeshlets.size())
                {
                    for (uint32_t i = 0; i < gpuMesh.meshletCount; ++i)
                    {
                        const auto& meshlet = pool.meshlets.cpuMeshlets[gpuMesh.meshletOffset + i];
                        expandBounds(out, tr.worldMatrix, meshlet.center, meshlet.radius);
                    }
                }
            }
        }

        auto splatView = reg.view<IDComponent, TransformComponent, GaussianSplatComponent>();
        out.gaussianSplats.reserve(splatView.size_hint());
        for (auto e : splatView)
        {
            const auto& id    = splatView.get<IDComponent>(e);
            const auto& tr    = splatView.get<TransformComponent>(e);
            const auto& splat = splatView.get<GaussianSplatComponent>(e);
            if (!isEntityRenderable(world, reg, e))
                continue;

            auto h = assets.loadGaussianSplatSync(splat.gaussianSplat);
            if (!h.ready())
                continue;

            RenderGaussianSplatInstance inst {};
            inst.entity      = id.uuid;
            inst.splatIndex  = h.gpuIndex();
            inst.worldMatrix = tr.worldMatrix;
            out.gaussianSplats.push_back(inst);
        }

        auto lightView = reg.view<IDComponent, TransformComponent, LightComponent>();
        out.lights.reserve(lightView.size_hint());
        for (auto e : lightView)
        {
            const auto& id    = lightView.get<IDComponent>(e);
            const auto& tr    = lightView.get<TransformComponent>(e);
            const auto& light = lightView.get<LightComponent>(e);
            if (!isEntityRenderable(world, reg, e))
                continue;

            RenderLight outLight {};
            outLight.entity           = id.uuid;
            outLight.kind             = static_cast<RenderLightKind>(light.kind);
            outLight.position         = glm::vec3(tr.worldMatrix[3]);
            outLight.direction        = lightDirectionFromTransformNormal(tr);
            outLight.color            = light.color;
            outLight.intensity        = light.intensity;
            outLight.range            = light.range;
            outLight.radius           = light.radius;
            outLight.width            = light.width;
            outLight.height           = light.height;
            outLight.innerConeDegrees = light.innerConeDegrees;
            outLight.outerConeDegrees = light.outerConeDegrees;
            outLight.castsShadow      = light.castsShadow;
            outLight.twoSided         = light.twoSided;
            out.lights.push_back(outLight);

            if (outLight.kind == RenderLightKind::eArea)
            {
                const uint32_t meshIndex =
                    geometryFactory.getOrCreateMeshIndex(BuiltinGeometryKind::eQuad, gpuResources, rd);
                if (meshIndex != std::numeric_limits<uint32_t>::max())
                {
                    RenderInstance surface {};
                    surface.entity               = id.uuid;
                    surface.meshIndex            = meshIndex;
                    surface.worldMatrix          = areaLightSurfaceMatrix(outLight);
                    surface.baseColorOverride    = glm::vec4(outLight.color, 1.0f);
                    surface.hasBaseColorOverride = true;
                    surface.castsShadow          = false;
                    out.instances.push_back(surface);

                    const auto& pool = gpuResources.pool();
                    if (meshIndex < pool.meshes.size())
                    {
                        const auto& gpuMesh = pool.meshes[meshIndex];
                        if (gpuMesh.meshletCount > 0 &&
                            gpuMesh.meshletOffset + gpuMesh.meshletCount <= pool.meshlets.cpuMeshlets.size())
                        {
                            for (uint32_t i = 0; i < gpuMesh.meshletCount; ++i)
                            {
                                const auto& meshlet = pool.meshlets.cpuMeshlets[gpuMesh.meshletOffset + i];
                                expandBounds(out, surface.worldMatrix, meshlet.center, meshlet.radius);
                            }
                        }
                    }
                }
            }
        }

        auto environmentView = reg.view<EnvironmentComponent>();
        for (auto e : environmentView)
        {
            const auto& environment = environmentView.get<EnvironmentComponent>(e);
            if (!environment.active)
                continue;
            if (!isEntityRenderable(world, reg, e))
                continue;

            out.environment.active           = true;
            out.environment.ambientColor     = environment.ambientColor;
            out.environment.ambientIntensity = environment.ambientIntensity;
            out.environment.enableIBL        = environment.enableIBL;
            out.environment.iblColor         = environment.iblColor;
            out.environment.iblIntensity     = environment.iblIntensity;

            if (environment.skybox.valid())
            {
                auto        skybox = assets.loadTextureSync(environment.skybox);
                const auto& pool   = gpuResources.pool();
                if (skybox.ready() && skybox.gpuIndex() < pool.textures.size())
                    out.environment.skybox = pool.textures[skybox.gpuIndex()].texture.get();
            }
            break;
        }

        auto reflectionProbeView = reg.view<IDComponent, TransformComponent, ReflectionProbeComponent>();
        out.reflectionProbes.reserve(reflectionProbeView.size_hint());
        for (auto e : reflectionProbeView)
        {
            const auto& id    = reflectionProbeView.get<IDComponent>(e);
            const auto& tr    = reflectionProbeView.get<TransformComponent>(e);
            const auto& probe = reflectionProbeView.get<ReflectionProbeComponent>(e);
            if (!probe.active)
                continue;
            if (!isEntityRenderable(world, reg, e))
                continue;
            rhi::Texture* environmentMap = nullptr;
            if (probe.enableIBL)
            {
                if (!probe.environmentMap.valid())
                    continue;
                auto        map  = assets.loadTextureSync(probe.environmentMap);
                const auto& pool = gpuResources.pool();
                if (!map.ready() || map.gpuIndex() >= pool.textures.size())
                    continue;
                environmentMap = pool.textures[map.gpuIndex()].texture.get();
            }

            if (probe.enableIBL && !environmentMap)
                continue;

            RenderReflectionProbe outProbe {};
            outProbe.entity             = id.uuid;
            outProbe.position           = glm::vec3(tr.worldMatrix[3]);
            outProbe.halfExtents        = glm::max(probe.boxSize * 0.5f, glm::vec3 {0.01f});
            outProbe.radius             = std::max(probe.radius, 0.01f);
            outProbe.blendDistance      = std::max(probe.blendDistance, 0.0f);
            outProbe.intensity          = std::max(probe.intensity, 0.0f);
            outProbe.priority           = probe.priority;
            outProbe.enableIBL          = probe.enableIBL;
            outProbe.parallaxCorrection = probe.parallaxCorrection;
            outProbe.shape = probe.shape == 1u ? RenderReflectionProbeShape::eSphere : RenderReflectionProbeShape::eBox;
            outProbe.environmentMap = environmentMap;
            out.reflectionProbes.push_back(outProbe);
        }
    }

    bool RenderSystem::onInit()
    {
        VULTRA_CORE_INFO("[RenderSystem] Initializing...");

#if defined(__ANDROID__)
        // Android paths currently rely on the CPU-driven renderer.
        m_EnableGpuDrivenMeshletPipeline = false;
#endif

        VULTRA_CORE_TRACE("[RenderSystem] Getting render backend service");
        auto& backendService = ctx().services.require<IRenderBackendService>();

        VULTRA_CORE_TRACE("[RenderSystem] Getting window service");
        auto& windowService = ctx().services.require<IWindowService>();

        VULTRA_CORE_TRACE("[RenderSystem] Creating transient resources");
        m_TransientResources = createScope<framegraph::TransientResources>(backendService.renderDevice());

        VULTRA_CORE_TRACE("[RenderSystem] Providing IRenderService");
        ctx().services.provide<IRenderService>(this);

        if (!ctx().config.render.renderPipelineAsset.empty())
        {
            auto* assetService = ctx().services.tryGet<IAssetService>();
            auto  pipelineText = assetService ?
                                     assetService->loadTextAssetSync(ctx().config.render.renderPipelineAsset) :
                                     vbase::Result<std::string, std::string>::err("asset service unavailable");
            if (pipelineText)
            {
                VULTRA_CORE_INFO("[RenderSystem] Using render pipeline '{}'", ctx().config.render.renderPipelineAsset);
                const auto rendererKey = ctx().config.render.renderPipelineRendererKey.empty() ?
                                             rendererKeyFromRenderGraphUri(ctx().config.render.renderPipelineAsset) :
                                             ctx().config.render.renderPipelineRendererKey;
                m_Renderers[rendererKey] =
                    createRef<DeclarativeRenderer>(ctx().config.render.renderPipelineAsset, rendererKey);
                m_DefaultRendererKey = rendererKey;
            }
            else
            {
                VULTRA_CORE_WARN(
                    "[RenderSystem] Render pipeline '{}' is unavailable: {}. Falling back to registered renderer.",
                    ctx().config.render.renderPipelineAsset,
                    std::move(pipelineText).error());
            }
        }

        VULTRA_CORE_TRACE("[RenderSystem] Initializing renderers");
        for (auto& [key, renderer] : m_Renderers)
        {
            VULTRA_CORE_TRACE("[RenderSystem]     Initializing renderer: {}", key);
            Services services = ctx().services;
            renderer->setupServices(services);
            renderer->init();
        }
        m_Initialized = true;

        VULTRA_CORE_TRACE("[RenderSystem] Initializing samplers");
        m_Samplers["default"] = backendService.renderDevice().getSampler(rhi::SamplerInfo {});
        m_Samplers["linear"]  = backendService.renderDevice().getSampler(
            rhi::SamplerInfo {.magFilter = rhi::TexelFilter::eLinear, .minFilter = rhi::TexelFilter::eLinear});
        m_Samplers["bilinear"] = m_Samplers["linear"];
        m_Samplers["nearest"]  = backendService.renderDevice().getSampler(
            rhi::SamplerInfo {.magFilter = rhi::TexelFilter::eNearest, .minFilter = rhi::TexelFilter::eNearest});

        const auto initialExtent = backendService.swapchain().getExtent();
        if (initialExtent.width > 0u && initialExtent.height > 0u)
            onResize(initialExtent.width, initialExtent.height);

        VULTRA_CORE_INFO("[RenderSystem] Initialized!");

        return true;
    }

    void RenderSystem::onShutdown()
    {
        VULTRA_CORE_INFO("[RenderSystem] Shutting down");

        auto& backendService = ctx().services.require<IRenderBackendService>();
        backendService.renderDevice().waitIdle();

        m_GpuSceneViewBack.clear();
        m_GpuSceneViewFront.clear();
        m_GpuSceneDatabaseBack.clear();
        m_GpuSceneDatabaseFront.clear();
        m_GpuSceneDirtyTracker.reset();

        for (auto& [key, renderer] : m_Renderers)
            renderer = nullptr;
        m_Renderers.clear();

        m_FrameGraphTextureCaptureEnabled = false;
        clearFrameGraphDebugState();

        m_TransientResources.reset();

        m_FrameResources.clear();
        m_Initialized = false;
    }

    void RenderSystem::registerRenderer(Ref<Renderer> renderer)
    {
        if (!renderer)
            return;
        if (m_Renderers.contains(std::string(renderer->name())))
            return;
        if (m_Initialized)
        {
            Services services = ctx().services;
            renderer->setupServices(services);
            renderer->init();
        }
        m_Renderers[std::string(renderer->name())] = renderer;
    }

    std::vector<std::string> RenderSystem::rendererKeys() const
    {
        std::vector<std::string> keys;
        keys.reserve(m_Renderers.size());
        for (const auto& [key, renderer] : m_Renderers)
        {
            if (renderer)
                keys.push_back(key);
        }
        std::sort(keys.begin(), keys.end());
        return keys;
    }

    Ref<Renderer> RenderSystem::resolveRenderer(const RenderCamera& cam) const
    {
        if (rendererRequiresRayTracingScene(cam.rendererKey))
        {
            const auto* renderBackend = ctx().services.tryGet<IRenderBackendService>();
            const bool  rayTracingAvailable =
                renderBackend &&
                HasFlagValues(const_cast<IRenderBackendService*>(renderBackend)->renderDevice().getFeatureFlag(),
                              rhi::RenderDeviceFeatureFlagBits::eRayTracingPipeline);
            if (!rayTracingAvailable)
            {
                if (auto fallback = m_Renderers.find("universal"); fallback != m_Renderers.end())
                    return fallback->second;
            }
        }

        if (auto it = m_Renderers.find(cam.rendererKey); it != m_Renderers.end())
            return it->second;

        if (auto it2 = m_Renderers.find(m_DefaultRendererKey); it2 != m_Renderers.end())
            return it2->second;

        return nullptr;
    }

    bool RenderSystem::rendererRequiresRayTracingScene(std::string_view rendererKey) const
    {
        if (rendererKeyRequiresRayTracingScene(rendererKey))
            return true;

        if (auto it = m_Renderers.find(std::string(rendererKey)); it != m_Renderers.end())
            return it->second && it->second->requiresRayTracingScene();

        return false;
    }

    void RenderSystem::onResize(uint32_t width, uint32_t height)
    {
        for (auto& [key, renderer] : m_Renderers)
        {
            if (renderer)
                renderer->onResize(width, height);
        }
    }

    void RenderSystem::resetSceneState()
    {
        m_RenderWorldFront.clear();
        m_RenderWorldBack.clear();
        m_GpuSceneViewBack.clear();
        m_GpuSceneViewFront.clear();
        m_GpuSceneDatabaseBack.clear();
        m_GpuSceneDatabaseFront.clear();
        m_GpuSceneDirtyTracker.reset();
        m_OverrideRenderWorlds.clear();
        m_GeometryFactory.clear();
    }

    bool RenderSystem::reloadRenderPipeline()
    {
        if (m_InRenderFrame)
        {
            m_PendingRenderPipelineReload = true;
            m_PendingRenderPipelineAsset.clear();
            m_PendingRenderPipelineRendererKey.clear();
            VULTRA_CORE_INFO("[RenderSystem] Queued render pipeline reload for next frame");
            return true;
        }

        return reloadRenderPipelineNow();
    }

    bool RenderSystem::reloadRenderPipeline(std::string_view asset, std::string_view rendererKey)
    {
        if (asset.empty())
            return false;

        if (m_InRenderFrame)
        {
            m_PendingRenderPipelineReload = true;
            m_PendingRenderPipelineAsset  = std::string {asset};
            m_PendingRenderPipelineRendererKey =
                rendererKey.empty() ? rendererKeyFromRenderGraphUri(asset) : std::string {rendererKey};
            VULTRA_CORE_INFO("[RenderSystem] Queued render pipeline reload for next frame");
            return true;
        }

        return reloadRenderPipelineNow(asset, rendererKey);
    }

    bool RenderSystem::reloadRenderPipelineNow()
    {
        const auto& renderConfig = ctx().config.render;
        if (renderConfig.renderPipelineAsset.empty())
            return false;

        if (auto* backendService = ctx().services.tryGet<IRenderBackendService>())
            backendService->renderDevice().waitIdle();
        clearFrameGraphDebugState();

        const auto rendererKey = renderConfig.renderPipelineRendererKey.empty() ?
                                     rendererKeyFromRenderGraphUri(renderConfig.renderPipelineAsset) :
                                     renderConfig.renderPipelineRendererKey;

        auto     renderer = createRef<DeclarativeRenderer>(renderConfig.renderPipelineAsset, rendererKey);
        Services services = ctx().services;
        renderer->setupServices(services);
        renderer->init();
        m_Renderers[rendererKey]      = std::move(renderer);
        m_DefaultRendererKey          = rendererKey;
        m_PendingRenderPipelineReload = false;
        VULTRA_CORE_INFO("[RenderSystem] Reloaded render pipeline '{}'", renderConfig.renderPipelineAsset);
        return true;
    }

    bool RenderSystem::reloadRenderPipelineNow(std::string_view asset, std::string_view rendererKey)
    {
        if (asset.empty())
            return false;

        if (auto* backendService = ctx().services.tryGet<IRenderBackendService>())
            backendService->renderDevice().waitIdle();
        clearFrameGraphDebugState();

        auto     key      = rendererKey.empty() ? rendererKeyFromRenderGraphUri(asset) : std::string {rendererKey};
        auto     renderer = createRef<DeclarativeRenderer>(std::string {asset}, key);
        Services services = ctx().services;
        renderer->setupServices(services);
        renderer->init();
        m_Renderers[key]              = std::move(renderer);
        m_PendingRenderPipelineReload = false;
        m_PendingRenderPipelineAsset.clear();
        m_PendingRenderPipelineRendererKey.clear();
        VULTRA_CORE_INFO("[RenderSystem] Reloaded render pipeline '{}'", asset);
        return true;
    }

    void RenderSystem::clearFrameGraphDebugState()
    {
        m_FrameGraphTexturePreviewPipeline.reset();
        m_FrameGraphTexturePreviewPipelineFormat = rhi::PixelFormat::eUndefined;
        m_FrameGraphDebugTextures.clear();
        m_FrameGraphDebugTextureSlots.clear();
        m_RetiredFrameGraphDebugTextureSlots.clear();
        m_FrameGraphTexturePreviewOverrides.clear();
        m_FrameGraphTexturePreviewSettings = {};
        m_LastFrameGraphSnapshot.clear();
    }

    void RenderSystem::releaseOverrideRenderWorld(World* world)
    {
        if (!world)
            return;

        for (auto it = m_OverrideRenderWorlds.begin(); it != m_OverrideRenderWorlds.end();)
        {
            if (it->world == world)
                it = m_OverrideRenderWorlds.erase(it);
            else
                ++it;
        }
    }

    rhi::GraphicsPipeline* RenderSystem::getFrameGraphTexturePreviewPipeline(rhi::RenderDevice&         rd,
                                                                             rhi::ShaderLibraryRuntime& shaderLib,
                                                                             const rhi::PixelFormat     colorFormat)
    {
        if (m_FrameGraphTexturePreviewPipeline && m_FrameGraphTexturePreviewPipelineFormat == colorFormat)
        {
            return &*m_FrameGraphTexturePreviewPipeline;
        }

        const auto vertexHash = rhi::ShaderLibraryRuntime::computeVariantHash(
            "fullscreen_triangle.vert", vshadersystem::ShaderStage::eVert, {});
        const auto fragmentHash = rhi::ShaderLibraryRuntime::computeVariantHash(
            "frame_debugger_texture_preview.frag", vshadersystem::ShaderStage::eFrag, {});
        auto vertexShader   = shaderLib.load(vertexHash, vshadersystem::ShaderStage::eVert);
        auto fragmentShader = shaderLib.load(fragmentHash, vshadersystem::ShaderStage::eFrag);
        if (!vertexShader || !fragmentShader)
        {
            VULTRA_CORE_ERROR("[FrameDebugger] Failed to load texture preview shaders");
            m_FrameGraphTexturePreviewPipeline.reset();
            m_FrameGraphTexturePreviewPipelineFormat = rhi::PixelFormat::eUndefined;
            return nullptr;
        }

        m_FrameGraphTexturePreviewPipeline = rhi::GraphicsPipeline::Builder {}
                                                 .setColorFormats({colorFormat})
                                                 .setInputAssembly({})
                                                 .addBuiltinShader(rhi::ShaderType::eVertex, *vertexShader)
                                                 .addBuiltinShader(rhi::ShaderType::eFragment, *fragmentShader)
                                                 .setDepthStencil({
                                                     .depthTest  = false,
                                                     .depthWrite = false,
                                                 })
                                                 .setRasterizer({
                                                     .polygonMode = rhi::PolygonMode::eFill,
                                                     .cullMode    = rhi::CullMode::eNone,
                                                 })
                                                 .setBlending(0, {.enabled = false})
                                                 .build(rd);
        m_FrameGraphTexturePreviewPipelineFormat = colorFormat;
        return m_FrameGraphTexturePreviewPipeline ? &*m_FrameGraphTexturePreviewPipeline : nullptr;
    }

    void RenderSystem::addFrameGraphTextureCapturePasses(FrameGraphBuildContext& ctx, const RenderCamera& camera)
    {
        if (!m_FrameGraphTextureCaptureEnabled)
            return;
        if (ctx.rd.getBackendApi() == rhi::RenderBackendApi::eWebGPU)
            return;

        struct CaptureCandidate
        {
            FrameGraphResource resource {};
            uint32_t           resourceNodeId {0};
            uint32_t           resourceVersion {0};
            std::string        name;
            rhi::ImageAspect   aspect {rhi::ImageAspect::eColor};
            uint32_t           layer {0};
            uint32_t           layerCount {1};
            bool               imported {false};
            bool               capturable {false};
        };

        struct PassData
        {
            FrameGraphResource source;
            FrameGraphResource target;
        };

        struct alignas(16) PreviewPushConstants
        {
            glm::ivec4 channelMask {1, 1, 1, 1};
            int        gammaCorrect {0};
            int        previewMode {0};
            float      depthNear {0.1f};
            float      depthFar {1000.0f};
            float      clampMin {0.0f};
            float      clampMax {1.0f};
        };

        struct TextureResourceCollector
        {
            std::vector<CaptureCandidate> candidates;

            static rhi::ImageAspect imageAspectFor(rhi::PixelFormat format)
            {
                const auto aspectMask = rhi::getAspectMask(format);
                if (HasFlagValues(aspectMask, rhi::ImageAspectFlags::eDepth))
                    return rhi::ImageAspect::eDepth;
                return rhi::ImageAspect::eColor;
            }

            static bool canPreviewWithFloatSampler(rhi::PixelFormat format)
            {
                switch (format)
                {
                    using enum rhi::PixelFormat;

                    case eR8UI:
                    case eR8I:
                    case eRG8UI:
                    case eRG8I:
                    case eRGBA8UI:
                    case eRGBA8I:
                    case eR16UI:
                    case eR16I:
                    case eRG16UI:
                    case eRG16I:
                    case eRGBA16UI:
                    case eRGBA16I:
                    case eR32UI:
                    case eR32I:
                    case eRG32UI:
                    case eRG32I:
                    case eRGBA32UI:
                    case eRGBA32I:
                    case eStencil8:
                        return false;
                    default:
                        return format != eUndefined;
                }
            }

            void operator()(const PassNode&, const std::vector<ResourceNode>&) {}

            void operator()(const ResourceNode& resource, const ResourceEntry& entry, const std::vector<PassNode>&)
            {
                // fg has no public runtime type tag. FrameGraphTexture::toString includes usage metadata;
                // buffer resources do not, so this keeps capture automatic without touching fg internals.
                if (entry.toString().find("<BR/>Usage = ") == std::string::npos)
                    return;

                const auto& desc = entry.getDescriptor<framegraph::FrameGraphTexture>();
                if (desc.format == rhi::PixelFormat::eUndefined || desc.extent.width == 0u || desc.extent.height == 0u)
                    return;

                std::string name(resource.getName());
                if (resource.getVersion() > ResourceEntry::kInitialVersion)
                    name += " v" + std::to_string(resource.getVersion());
                const auto layerCount = std::max(desc.layers, 1u);
                const bool capturable = canPreviewWithFloatSampler(desc.format) &&
                                        static_cast<bool>(desc.usageFlags & rhi::ImageUsage::eSampled);
                for (uint32_t layer = 0u; layer < layerCount; ++layer)
                {
                    auto layerName = name;
                    if (layerCount > 1u)
                        layerName += layer == 0u ? " [Left Eye]" :
                                     layer == 1u ? " [Right Eye]" :
                                                   " [Layer " + std::to_string(layer) + "]";
                    candidates.push_back(CaptureCandidate {
                        .resource        = static_cast<FrameGraphResource>(resource.getId()),
                        .resourceNodeId  = resource.getResourceId(),
                        .resourceVersion = resource.getVersion(),
                        .name            = std::move(layerName),
                        .aspect          = imageAspectFor(desc.format),
                        .layer           = layer,
                        .layerCount      = layerCount,
                        .imported        = entry.isImported(),
                        .capturable      = capturable,
                    });
                }
            }

            void flush(std::ostream&) const {}
        };

        std::ostringstream       unused;
        TextureResourceCollector collector;
        ctx.fg.debugOutput(unused, collector);

        for (const auto& candidate : collector.candidates)
        {
            auto       source     = candidate.resource;
            const auto sourceDesc = ctx.fg.getDescriptor<framegraph::FrameGraphTexture>(source);
            if (sourceDesc.format == rhi::PixelFormat::eUndefined || sourceDesc.extent.width == 0u ||
                sourceDesc.extent.height == 0u)
            {
                continue;
            }

            std::string cameraName {camera.name.empty() ? std::string {"Camera"} : camera.name};
            std::string slotKey = cameraName + "/" + candidate.name + "/layer:" + std::to_string(candidate.layer);
            std::string transientResourceKey = "R" + std::to_string(candidate.resourceNodeId) + "_" +
                                               std::to_string(candidate.resourceVersion) + "/layer:" +
                                               std::to_string(candidate.layer);
            const auto overrideIt          = m_FrameGraphTexturePreviewOverrides.find(slotKey);
            const auto previewSettings     = overrideIt != m_FrameGraphTexturePreviewOverrides.end() ?
                                                 overrideIt->second :
                                                 m_FrameGraphTexturePreviewSettings;
            const bool captureAllRequested = m_FrameGraphTexturePreviewSettings.selectedTextureKey ==
                                             FrameGraphTexturePreviewSettings::kCaptureAllTextures;
            const bool captureNoneRequested = m_FrameGraphTexturePreviewSettings.selectedTextureKey ==
                                              FrameGraphTexturePreviewSettings::kCaptureNoTextures;
            const bool shouldPreview = captureAllRequested ?
                                           true :
                                       captureNoneRequested ?
                                           false :
                                       m_FrameGraphTexturePreviewSettings.selectedTextureKey.empty() ?
                                           m_FrameGraphDebugTextures.empty() :
                                           slotKey == m_FrameGraphTexturePreviewSettings.selectedTextureKey;
            auto previewExtent = sourceDesc.extent;
            if (previewSettings.maxPreviewExtent > 0u)
            {
                const uint32_t maxSourceExtent = std::max(sourceDesc.extent.width, sourceDesc.extent.height);
                if (maxSourceExtent > previewSettings.maxPreviewExtent)
                {
                    previewExtent.width = std::max(
                        1u,
                        static_cast<uint32_t>((static_cast<uint64_t>(sourceDesc.extent.width) *
                                               previewSettings.maxPreviewExtent) /
                                              maxSourceExtent));
                    previewExtent.height = std::max(
                        1u,
                        static_cast<uint32_t>((static_cast<uint64_t>(sourceDesc.extent.height) *
                                               previewSettings.maxPreviewExtent) /
                                              maxSourceExtent));
                }
            }
            std::string publicKey = slotKey + "@" + std::to_string(previewExtent.width) + "x" +
                                    std::to_string(previewExtent.height) + ":" +
                                    std::string(rhi::toString(sourceDesc.format));
            if (!candidate.capturable || !shouldPreview)
            {
                m_FrameGraphDebugTextures.push_back(FrameGraphDebugTexture {
                    .camera               = cameraName,
                    .renderer             = camera.rendererKey,
                    .name                 = candidate.name,
                    .key                  = publicKey,
                    .resourceKey          = slotKey,
                    .transientResourceKey = transientResourceKey,
                    .texture              = nullptr,
                    .layer                = candidate.layer,
                    .layerCount           = candidate.layerCount,
                    .imported             = candidate.imported,
                    .capturable           = candidate.capturable,
                    .extent               = previewExtent,
                    .sourceExtent         = sourceDesc.extent,
                    .format               = sourceDesc.format,
                    .zNear                = camera.zNear,
                    .zFar                 = camera.zFar,
                });
                continue;
            }

            auto&      slot     = m_FrameGraphDebugTextureSlots[publicKey];
            const bool recreate = !slot.texture || slot.extent.width != previewExtent.width ||
                                  slot.extent.height != previewExtent.height ||
                                  slot.format != rhi::PixelFormat::eRGBA8_UNorm;
            if (recreate)
            {
                if (slot.texture)
                {
                    slot.lastTouchedFrame = m_FrameCounter;
                    m_RetiredFrameGraphDebugTextureSlots.push_back(std::move(slot));
                    slot = {};
                }

                slot.texture = rhi::Texture::Builder {}
                                   .setExtent(previewExtent)
                                   .setPixelFormat(rhi::PixelFormat::eRGBA8_UNorm)
                                   .setNumMipLevels(1)
                                   .setUsageFlags(rhi::ImageUsage::eSampled | rhi::ImageUsage::eRenderTarget |
                                                  rhi::ImageUsage::eTransferSrc)
                                   .build(ctx.rd);
            }

            slot.camera           = cameraName;
            slot.name             = candidate.name;
            slot.key              = publicKey;
            slot.extent           = previewExtent;
            slot.format           = rhi::PixelFormat::eRGBA8_UNorm;
            slot.lastTouchedFrame = m_FrameCounter;

            if (!slot.texture)
                continue;

            auto target = framegraph::importTexture(ctx.fg, std::string {"DebugCapture/"} + slotKey, &*slot.texture);
            ctx.fg.addCallbackPass<PassData>(
                std::string {"DebugCapture/"} + candidate.name,
                [source, target, aspect = candidate.aspect](FrameGraph::Builder& builder, PassData& pd) {
                    pd.source = builder.read(source,
                                             framegraph::TextureRead {
                                                 .binding =
                                                     {
                                                         .location      = {.set = 3, .binding = 0},
                                                         .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                                     },
                                                 .type        = framegraph::TextureRead::Type::eCombinedImageSampler,
                                                 .imageAspect = aspect,
                                             });
                    pd.target = builder.write(target,
                                              framegraph::Attachment {
                                                  .index       = 0,
                                                  .imageAspect = rhi::ImageAspect::eColor,
                                                  .clearValue  = framegraph::ClearValue::eOpaqueBlack,
                                              });
                },
                [this, preview = previewSettings, sourceLayer = candidate.layer, sourceAspect = candidate.aspect](
                    const PassData& data, FrameGraphPassResources& resources, void* ctxPtr) {
                    VULTRA_SCOPED_FRAMEGRAPH_EXEC_CONTEXT(rc, ctxPtr);
                    if (!rc.ext.builtinShaderLib)
                        return;
                    assert(rc.framebufferInfo().has_value());
                    const auto fb       = rc.framebufferInfo().value();
                    auto*      pipeline = getFrameGraphTexturePreviewPipeline(
                        rc.rd,
                        *rc.ext.builtinShaderLibForProfile(rhi::ShaderProfile::eGeneral),
                        rhi::getColorFormat(fb, 0));
                    if (!pipeline)
                        return;

                    PreviewPushConstants pc {
                        .channelMask = glm::ivec4(preview.channels[0] ? 1 : 0,
                                                  preview.channels[1] ? 1 : 0,
                                                  preview.channels[2] ? 1 : 0,
                                                  preview.channels[3] ? 1 : 0),
                        .gammaCorrect = preview.gammaCorrect ? 1 : 0,
                        .previewMode  = preview.previewMode,
                        .depthNear    = preview.depthNear,
                        .depthFar     = preview.depthFar,
                        .clampMin     = preview.clampMin,
                        .clampMax     = preview.clampMax,
                    };

                    auto*      sourceTexture = resources.get<framegraph::FrameGraphTexture>(data.source).texture;
                    const auto samplerIt     = rc.ext.samplers.find("nearest");
                    if (sourceTexture && samplerIt != rc.ext.samplers.end())
                    {
                        rc.resourceSet[3][0] = rhi::bindings::CombinedImageSampler {
                            .texture     = sourceTexture,
                            .imageAspect = sourceAspect,
                            .sampler     = samplerIt->second,
                            .layer = sourceTexture->getNumLayers() > 1u ? std::optional {sourceLayer} : std::nullopt,
                        };
                    }
                    rc.cb.bindPipeline(*pipeline);
                    rc.bindDescriptorSets(*pipeline);
                    rc.cb.pushConstants(rhi::ShaderStages::eFragment, 0, &pc);
                    rc.cb.beginRendering(fb).drawFullScreenTriangle().endRendering();
                });

            m_FrameGraphDebugTextures.push_back(FrameGraphDebugTexture {
                .camera               = slot.camera,
                .renderer             = camera.rendererKey,
                .name                 = slot.name,
                .key                  = slot.key,
                .resourceKey          = slotKey,
                .transientResourceKey = transientResourceKey,
                .texture              = &*slot.texture,
                .layer                = candidate.layer,
                .layerCount           = candidate.layerCount,
                .imported             = candidate.imported,
                .capturable           = candidate.capturable,
                .extent               = slot.extent,
                .sourceExtent         = sourceDesc.extent,
                .format               = sourceDesc.format,
                .zNear                = camera.zNear,
                .zFar                 = camera.zFar,
            });
        }
    }

    void RenderSystem::renderFrame()
    {
        m_InRenderFrame = true;
        struct RenderFrameGuard
        {
            bool& value;
            ~RenderFrameGuard() { value = false; }
        } renderFrameGuard {m_InRenderFrame};

        const auto renderFrameCpuStart = std::chrono::steady_clock::now();

        auto& backendService     = ctx().services.require<IRenderBackendService>();
        auto& worldService       = ctx().services.require<IWorldService>();
        auto& camService         = ctx().services.require<ICameraService>();
        auto& gpuResourceService = ctx().services.require<IGpuResourceService>();
        auto& assetService       = ctx().services.require<IAssetService>();
        auto& shaderService      = ctx().services.require<IShaderService>();
        auto& window             = ctx().services.require<IWindowService>().window();

        // Optional ImGui service for rendering ImGui on top of frame.
        auto* imguiService = ctx().services.tryGet<IImGuiService>();

        // Optional frame debugger service for GPU capture.
        auto* frameDebuggerService = ctx().services.tryGet<IFrameDebuggerService>();

        auto& rd = backendService.renderDevice();

        RuntimeProfiler::setExternalSink(&m_RuntimeProfiler);
        if (m_PendingRenderPipelineReload)
        {
            if (m_PendingRenderPipelineAsset.empty())
                reloadRenderPipelineNow();
            else
                reloadRenderPipelineNow(m_PendingRenderPipelineAsset, m_PendingRenderPipelineRendererKey);
        }

        m_RuntimeProfiler.beginFrame(m_FrameCounter);
        m_LastFrameGraphSnapshot.clear();
        if (m_FrameGraphTextureCaptureEnabled)
            m_FrameGraphDebugTextures.clear();
        {
            constexpr uint64_t kDebugTextureReleaseDelayFrames = 8u;
            std::size_t        out                             = 0;
            for (auto& slot : m_RetiredFrameGraphDebugTextureSlots)
            {
                if (m_FrameCounter > slot.lastTouchedFrame + kDebugTextureReleaseDelayFrames)
                {
                    slot.texture.reset();
                }
                else
                {
                    m_RetiredFrameGraphDebugTextureSlots[out++] = std::move(slot);
                }
            }
            m_RetiredFrameGraphDebugTextureSlots.resize(out);
        }
        m_RuntimeProfiler.setVsyncEnabled(ctx().config.render.vSyncConfig != rhi::VerticalSync::eDisabled);
        rhi::CommandBuffer::resetFrameStats();

        // Begin frame first so downstream systems can consume per-frame backend state (e.g. XR eye views).
        if (!backendService.beginFrame())
        {
            m_SkipRender = true;
            m_RuntimeProfiler.endFrame();
            return;
        }

        auto&                  cb = backendService.commandBuffer();
        RuntimeProfiler::Scope scopeRenderFrame {m_RuntimeProfiler, "RenderSystem::renderFrame"};
        const bool profilerCaptureEnabled = m_RuntimeProfiler.isEnabled();
        const bool gpuTimingEnabled       = profilerCaptureEnabled && !isTrackyGpuProfilerEnabled();
        if (gpuTimingEnabled)
        {
            rd.beginFrameGpuQuery(cb);
        }

        // Default target for cameras without explicit RT
        auto& defaultTarget = backendService.backbuffer();

        if (frameDebuggerService)
        {
            frameDebuggerService->captureStart();
        }

        World&                              world         = worldService.world();
        const auto                          cookedCameras = camService.cameras();
        const std::span<const RenderCamera> cams          = cookedCameras;
        const bool rayTracingSceneRequired = std::any_of(cams.begin(), cams.end(), [this](const RenderCamera& cam) {
            return rendererRequiresRayTracingScene(cam.rendererKey);
        });
        const bool rayTracingAvailable =
            rayTracingSceneRequired &&
            HasFlagValues(rd.getFeatureFlag(), rhi::RenderDeviceFeatureFlagBits::eRayTracingPipeline);

        // Asset upload/update stage (main thread)
        {
            RuntimeProfiler::Scope scope {m_RuntimeProfiler, "AssetService::update"};
            assetService.update(m_FrameCounter);
        }
        // Cook render instances
        RenderWorldCooker cooker {};
        const float       renderTimeSeconds = static_cast<float>(m_FrameCounter) / 60.0f;
        {
            RuntimeProfiler::Scope scope {m_RuntimeProfiler, "RenderWorldCooker::cook"};
            cooker.cook(
                world, assetService, gpuResourceService, rd, m_GeometryFactory, m_RenderWorldBack, renderTimeSeconds);
        }
        m_RenderWorldBack.frameIndex = m_FrameCounter;

        const bool gaussianOrderedClodMode = m_GaussianSplatSettings.orderedClodEnabled();

        const uint64_t resourceRevision = gpuResourceService.contentRevision();
        const auto&    pool             = gpuResourceService.pool();

        uint32_t maxGeneralGaussianSplatPoints      = 0;
        uint32_t maxGeneralGaussianSplatSourceCount = 0;
        for (const auto& splatInst : m_RenderWorldBack.gaussianSplats)
        {
            if (splatInst.splatIndex >= pool.gaussianSplats.size())
                continue;
            const auto& gpuSplat = pool.gaussianSplats[splatInst.splatIndex];
            maxGeneralGaussianSplatPoints += gpuSplat.pointCount;
            maxGeneralGaussianSplatSourceCount += gpuSplat.pointCount;
        }

        GaussianSplatFrameStats gaussianStats {};
        gaussianStats.frameIndex                      = m_FrameCounter;
        gaussianStats.baselineMode                    = m_GaussianSplatSettings.baselineMode;
        gaussianStats.foveatedRenderMode              = m_GaussianSplatSettings.foveatedRenderMode;
        gaussianStats.lodBudgetEnabled                = m_GaussianSplatSettings.lodBudgetEnabled();
        gaussianStats.foveatedClodEnabled             = m_GaussianSplatSettings.foveatedClodActive();
        gaussianStats.foveatedLayeredCompositeEnabled = m_GaussianSplatSettings.foveatedLayeredCompositeActive();
        gaussianStats.foveatedBudgetControllerEnabled = m_GaussianSplatSettings.foveatedBudgetControllerEnabled;
        gaussianStats.lodBudget                       = m_GaussianSplatSettings.lodBudget;
        gaussianStats.foveatedRingLevels              = m_GaussianSplatSettings.foveatedRingLevels;
        gaussianStats.foveatedResolutionScales        = m_GaussianSplatSettings.foveatedResolutionScales;
        gaussianStats.foveatedRingDegrees             = m_GaussianSplatSettings.foveatedRingDegrees;
        gaussianStats.foveatedTargetFrameMs           = m_GaussianSplatSettings.foveatedTargetFrameMs;
        gaussianStats.splatAssets                     = static_cast<uint32_t>(m_RenderWorldBack.gaussianSplats.size());
        gaussianStats.totalSplats                     = maxGeneralGaussianSplatPoints;

        const bool gaussianModeSettingsDirty =
            m_GaussianSplatSettings.baselineMode != m_AppliedGaussianSplatSettings.baselineMode;
        const bool gaussianSelectionSettingsDirty =
            gaussianSplatSelectionSettingsDirty(m_GaussianSplatSettings, m_AppliedGaussianSplatSettings);
        bool gpuSceneTopologyDirty =
            gaussianModeSettingsDirty || m_GpuSceneDirtyTracker.shouldRebuildTopology(
                                             m_RenderWorldBack, resourceRevision, m_EnableGpuDrivenMeshletPipeline);
        bool gpuSceneTransformDirty =
            !gpuSceneTopologyDirty && m_GpuSceneDirtyTracker.shouldUpdateTransforms(m_RenderWorldBack);
        if (gpuSceneTransformDirty &&
            (m_GpuSceneDatabaseFront.transforms.size() != m_RenderWorldBack.instances.size() ||
             m_GpuSceneDatabaseFront.instances.size() != m_RenderWorldBack.instances.size()))
        {
            gpuSceneTopologyDirty  = true;
            gpuSceneTransformDirty = false;
        }
        if (!gpuSceneTopologyDirty && !gpuSceneTransformDirty && rayTracingAvailable &&
            !m_RenderWorldBack.instances.empty() &&
            (!m_GpuSceneDatabaseFront.rayTracingTlas || !m_GpuSceneDatabaseFront.rayTracingInstanceBuffer ||
             !m_GpuSceneDatabaseFront.rayTracingGeometryNodeBuffer))
        {
            gpuSceneTopologyDirty = true;
        }
        const bool gpuSceneDirty          = gpuSceneTopologyDirty || gpuSceneTransformDirty;
        const bool gaussianSelectionDirty = gaussianOrderedClodMode && gaussianSelectionSettingsDirty;

        // Build GPU scene database + per-view draw state.
        //
        // Database layer:
        // - stable pointer to global resource pool
        // - scene/instance tables
        //
        // View layer:
        // - draw table
        // - indirect commands
        if (gpuSceneTransformDirty)
        {
            RuntimeProfiler::Scope scope {m_RuntimeProfiler, "GpuScene::update_transforms"};

            auto& gpuSceneDatabase = m_GpuSceneDatabaseFront;
            auto& gpuSceneView     = m_GpuSceneViewFront;

            for (uint32_t instanceIndex = 0; instanceIndex < static_cast<uint32_t>(m_RenderWorldBack.instances.size());
                 ++instanceIndex)
            {
                const auto& model                          = m_RenderWorldBack.instances[instanceIndex].worldMatrix;
                gpuSceneDatabase.transforms[instanceIndex] = model;
            }
            gpuSceneDatabase.uploadTransforms(rd, cb);
            if (rayTracingAvailable)
            {
                if (gpuSceneDatabase.rayTracingInstances.empty() && !gpuSceneDatabase.instances.empty())
                    gpuSceneDatabase.rebuildRayTracingScene(rd);
                else
                    gpuSceneDatabase.rebuildRayTracingTlas(rd);
            }

            if (gpuSceneView.isCpuDriven())
            {
                for (auto& draw : gpuSceneView.draws)
                {
                    if (draw.instanceIndex < m_RenderWorldBack.instances.size())
                        draw.model = m_RenderWorldBack.instances[draw.instanceIndex].worldMatrix;
                }
                gpuSceneView.uploadDraws(rd, cb);
            }

            uint32_t gaussianDrawIndex = 0;
            for (const auto& splatInst : m_RenderWorldBack.gaussianSplats)
            {
                if (splatInst.splatIndex >= pool.gaussianSplats.size())
                    continue;

                const auto& gpuSplat = pool.gaussianSplats[splatInst.splatIndex];
                if (gpuSplat.pointCount == 0u)
                    continue;

                if (gaussianDrawIndex >= gpuSceneView.generalGaussianSplatDraws.size())
                    break;

                gpuSceneView.generalGaussianSplatDraws[gaussianDrawIndex].model = splatInst.worldMatrix;
                ++gaussianDrawIndex;
            }

            if (!gpuSceneView.generalGaussianSplatDraws.empty() && gpuSceneView.generalGaussianSplatDrawBuffer)
            {
                cb.update(*gpuSceneView.generalGaussianSplatDrawBuffer,
                          0,
                          static_cast<uint64_t>(gpuSceneView.generalGaussianSplatDraws.size()) *
                              sizeof(resource::GpuGeneralGaussianSplatDrawRecord),
                          gpuSceneView.generalGaussianSplatDraws.data());
            }

            if (gpuSceneView.generalGaussianSplatVisibleCountBuffer)
            {
                const uint32_t zero = 0u;
                cb.update(*gpuSceneView.generalGaussianSplatVisibleCountBuffer, 0, sizeof(uint32_t), &zero);
            }

            if (gpuSceneView.generalGaussianSplatDispatchArgsBuffer)
            {
                const uint32_t zeroArgs[4] = {0u, 1u, 1u, 0u};
                cb.update(*gpuSceneView.generalGaussianSplatDispatchArgsBuffer, 0, sizeof(zeroArgs), zeroArgs);
            }

            resetGaussianSplatIndirectBuffers(rd, gpuSceneView);

            m_RenderWorldBack.gpuSceneDatabase = &m_GpuSceneDatabaseFront;
            m_RenderWorldBack.gpuSceneView     = &m_GpuSceneViewFront;
        }
        else if (gpuSceneTopologyDirty)
        {
            RuntimeProfiler::Scope scope {m_RuntimeProfiler, "GpuScene::rebuild"};
            auto                   packGaussianCovariance = [](const glm::uvec4 packed) {
                const glm::vec2 p0 = glm::unpackHalf2x16(packed.x);
                const glm::vec2 p1 = glm::unpackHalf2x16(packed.y);
                const glm::vec2 p2 = glm::unpackHalf2x16(packed.z);

                glm::mat3 sigma(0.0f);
                sigma[0][0] = p0.x;
                sigma[1][0] = p0.y;
                sigma[0][1] = p0.y;
                sigma[2][0] = p1.x;
                sigma[0][2] = p1.x;
                sigma[1][1] = p1.y;
                sigma[2][1] = p2.x;
                sigma[1][2] = p2.x;
                sigma[2][2] = p2.y;
                return sigma;
            };
            auto repackGaussianCovariance = [](const glm::mat3& sigma) {
                return glm::uvec4 {
                    glm::packHalf2x16(glm::vec2(sigma[0][0], sigma[1][0])),
                    glm::packHalf2x16(glm::vec2(sigma[2][0], sigma[1][1])),
                    glm::packHalf2x16(glm::vec2(sigma[2][1], sigma[2][2])),
                    0u,
                };
            };

            m_GpuSceneDatabaseBack.beginFrame(pool);
            m_GpuSceneDatabaseBack.instances.reserve(m_RenderWorldBack.instances.size());
            m_GpuSceneDatabaseBack.transforms.reserve(m_RenderWorldBack.instances.size());
            m_GpuSceneDatabaseBack.rebuildMeshTableFromResources();

            // Keep CPU staging mirrors even though the current render path is still
            // CPU-driven. The upcoming GPU-driven cluster pipeline will consume the
            // same scene database buffers directly.
            for (const auto& inst : m_RenderWorldBack.instances)
            {
                const uint32_t transformIndex = m_GpuSceneDatabaseBack.pushTransform(inst.worldMatrix);

                resource::GpuInstance gpuInst {};
                gpuInst.meshIndex      = inst.meshIndex;
                gpuInst.materialIndex  = inst.materialIndex;
                gpuInst.transformIndex = transformIndex;
                gpuInst.flags          = 0;
                m_GpuSceneDatabaseBack.pushInstance(gpuInst);
            }
            m_GpuSceneDatabaseBack.uploadSceneTables(rd, cb);

            if (rayTracingAvailable)
                m_GpuSceneDatabaseBack.rebuildRayTracingScene(rd);

            uint32_t maxMeshletDraws = 0;
            for (const auto& inst : m_RenderWorldBack.instances)
            {
                if (inst.meshIndex >= pool.meshes.size())
                    continue;
                maxMeshletDraws += pool.meshes[inst.meshIndex].meshletCount;
            }

            if (m_EnableGpuDrivenMeshletPipeline)
            {
                m_GpuSceneViewBack.beginFrame(m_GpuSceneDatabaseBack, resource::GpuSceneBuildMode::eGpuDriven);
                m_GpuSceneViewBack.setGpuDrivenCaps(
                    static_cast<uint32_t>(m_GpuSceneDatabaseBack.instances.size()), maxMeshletDraws, maxMeshletDraws);
            }
            else
            {
                m_GpuSceneViewBack.beginFrame(m_GpuSceneDatabaseBack, resource::GpuSceneBuildMode::eCpuDriven);
                m_GpuSceneViewBack.setGpuDrivenCaps(
                    static_cast<uint32_t>(m_GpuSceneDatabaseBack.instances.size()), maxMeshletDraws, maxMeshletDraws);
                m_GpuSceneViewBack.ensureVisibleMeshletBuffers(rd);
                m_GpuSceneViewBack.draws.reserve(maxMeshletDraws);

                for (uint32_t instanceIndex = 0;
                     instanceIndex < static_cast<uint32_t>(m_RenderWorldBack.instances.size());
                     ++instanceIndex)
                {
                    const auto& inst = m_RenderWorldBack.instances[instanceIndex];
                    if (inst.meshIndex >= pool.meshes.size())
                        continue;
                    if (instanceIndex >= m_GpuSceneDatabaseBack.instances.size())
                        continue;

                    const auto& mesh = pool.meshes[inst.meshIndex];
                    if (mesh.meshletCount == 0)
                        continue;

                    for (uint32_t localMeshlet = 0; localMeshlet < mesh.meshletCount; ++localMeshlet)
                    {
                        const uint32_t globalMeshletIndex = mesh.meshletOffset + localMeshlet;
                        if (globalMeshletIndex >= pool.meshlets.cpuMeshlets.size())
                            continue;

                        const auto& meshlet = pool.meshlets.cpuMeshlets[globalMeshletIndex];

                        resource::GpuDrawRecord dr;
                        dr.primitiveIndex       = globalMeshletIndex;
                        dr.materialIndex        = remapMaterialIndex(inst, mesh, meshlet.materialIndex);
                        dr.vertexStrideBytes    = mesh.vertexStrideBytes;
                        dr.flags                = resource::gpuDrawFlagsToMask(resource::GpuDrawFlags::eMeshlet);
                        dr.vertexAddress        = pool.geometry.vertexBytesAddress;
                        dr.instanceIndex        = instanceIndex;
                        const auto layout       = resource::inspectGpuVertexLayout(mesh.vertexAttributes);
                        dr.vertexAttributeMask  = layout.attributeMask;
                        dr.positionOffsetBytes  = layout.positionOffsetBytes;
                        dr.normalOffsetBytes    = layout.normalOffsetBytes;
                        dr.colorOffsetBytes     = layout.colorOffsetBytes;
                        dr.texCoord0OffsetBytes = layout.texCoord0OffsetBytes;
                        dr.texCoord1OffsetBytes = layout.texCoord1OffsetBytes;
                        dr.tangentOffsetBytes   = layout.tangentOffsetBytes;
                        dr.model                = inst.worldMatrix;
                        m_GpuSceneViewBack.pushMeshletDraw(std::move(dr));
                    }
                }

                std::stable_sort(
                    m_GpuSceneViewBack.draws.begin(), m_GpuSceneViewBack.draws.end(), [](const auto& a, const auto& b) {
                        if (a.materialIndex != b.materialIndex)
                            return a.materialIndex < b.materialIndex;
                        return a.primitiveIndex < b.primitiveIndex;
                    });

                m_GpuSceneViewBack.uploadDraws(rd, cb);
                m_GpuSceneViewBack.buildIndirectFromDraws(pool);
                m_GpuSceneViewBack.uploadIndirect(rd);
            }

            m_GpuSceneViewBack.generalGaussianSplatDraws.clear();
            m_GpuSceneViewBack.generalGaussianSplatPackedSources.clear();
            m_GpuSceneViewBack.generalGaussianSplatSelectedSources.clear();
            m_GpuSceneViewBack.generalGaussianSplatDirectPrefix = false;
            m_GpuSceneViewBack.generalGaussianSplatDraws.reserve(m_RenderWorldBack.gaussianSplats.size());
            m_GpuSceneViewBack.generalGaussianSplatPackedSources.reserve(maxGeneralGaussianSplatSourceCount);

            for (const auto& splatInst : m_RenderWorldBack.gaussianSplats)
            {
                if (splatInst.splatIndex >= pool.gaussianSplats.size())
                    continue;

                const auto& gpuSplat = pool.gaussianSplats[splatInst.splatIndex];
                if (gpuSplat.pointCount == 0u)
                    continue;

                const uint32_t drawIndex = static_cast<uint32_t>(m_GpuSceneViewBack.generalGaussianSplatDraws.size());
                const uint32_t pointBase = gpuSplat.pointOffset;
                const uint32_t shBaseStride = std::max(gpuSplat.shRestCoeffCount, 1u);
                const uint32_t rawSourceOffset =
                    static_cast<uint32_t>(m_GpuSceneViewBack.generalGaussianSplatPackedSources.size());

                resource::GpuGeneralGaussianSplatDrawRecord drawRecord {};
                drawRecord.splatIndex  = splatInst.splatIndex;
                drawRecord.pointOffset = rawSourceOffset;
                drawRecord.pointCount  = gpuSplat.pointCount;
                drawRecord.shDegree    = static_cast<uint32_t>(std::max(gpuSplat.shDegree, 0));
                // x: kernel size, y: cutoff scale, z: opacity scale, w: reserved sort order.
                drawRecord.params0 = glm::vec4 {0.3f, 1.0f, 1.0f, 0.0f};
                drawRecord.model   = splatInst.worldMatrix;

                for (uint32_t localPoint = 0; localPoint < gpuSplat.pointCount; ++localPoint)
                {
                    const uint32_t                                globalPoint = pointBase + localPoint;
                    resource::GpuGeneralGaussianSplatPackedSource packed {};
                    if (globalPoint < pool.gaussianStorage.cpuCenters.size() &&
                        globalPoint < pool.gaussianStorage.cpuCovariances.size() &&
                        globalPoint < pool.gaussianStorage.cpuColors.size())
                    {
                        const glm::vec4  localCenter = pool.gaussianStorage.cpuCenters[globalPoint];
                        const uint32_t   shOffset    = globalPoint * shBaseStride;
                        const glm::uvec2 sh0         = shOffset < pool.gaussianStorage.cpuSh.size() ?
                                                           pool.gaussianStorage.cpuSh[shOffset] :
                                                           glm::uvec2 {0u};

                        packed.posOpacity = glm::uvec4 {
                            std::bit_cast<uint32_t>(localCenter.x),
                            std::bit_cast<uint32_t>(localCenter.y),
                            std::bit_cast<uint32_t>(localCenter.z),
                            pool.gaussianStorage.cpuColors[globalPoint].y,
                        };
                        packed.covariance0 = pool.gaussianStorage.cpuCovariances[globalPoint];
                        packed.colorSh0    = glm::uvec4 {
                            pool.gaussianStorage.cpuColors[globalPoint].x,
                            pool.gaussianStorage.cpuColors[globalPoint].y,
                            sh0.x,
                            sh0.y,
                        };
                        packed.aux0 = glm::uvec4 {globalPoint, 0u, shOffset, 0u};
                    }
                    m_GpuSceneViewBack.pushGeneralGaussianSplatSource(packed);
                }

                m_GpuSceneViewBack.pushGeneralGaussianSplatDraw(drawRecord);
            }

            uint32_t       selectedSourceCapacity = 0u;
            uint32_t       activeGaussianSplats   = 0u;
            const uint32_t packedGaussianSources =
                static_cast<uint32_t>(m_GpuSceneViewBack.generalGaussianSplatPackedSources.size());
            const bool gaussianDirectPrefix =
                gaussianOrderedClodMode && m_GpuSceneViewBack.generalGaussianSplatDraws.size() == 1u;
            gaussianStats.directPrefix = gaussianDirectPrefix;
            if (gaussianDirectPrefix)
            {
                selectedSourceCapacity = 0u;
                activeGaussianSplats =
                    std::min(packedGaussianSources,
                             effectiveGaussianLodBudget(m_GaussianSplatSettings, maxGeneralGaussianSplatPoints));
                gaussianStats.lodSelectedRawSplats = activeGaussianSplats;
            }
            else if (gaussianOrderedClodMode)
            {
                rebuildGaussianSplatOrderedClodPrefixSources(
                    m_GpuSceneViewBack, m_RenderWorldBack.gaussianSplats, pool, m_RuntimeProfiler);
                selectedSourceCapacity =
                    static_cast<uint32_t>(m_GpuSceneViewBack.generalGaussianSplatSelectedSources.size());
                activeGaussianSplats =
                    std::min(selectedSourceCapacity,
                             effectiveGaussianLodBudget(m_GaussianSplatSettings, maxGeneralGaussianSplatPoints));
                gaussianStats.lodSelectedRawSplats = activeGaussianSplats;
            }
            else
            {
                rebuildGaussianSplatSelectedSources(
                    m_GpuSceneViewBack, m_RenderWorldBack.gaussianSplats, pool, gaussianStats, m_RuntimeProfiler);
                selectedSourceCapacity =
                    static_cast<uint32_t>(m_GpuSceneViewBack.generalGaussianSplatSelectedSources.size());
                activeGaussianSplats = selectedSourceCapacity;
            }

            uint32_t maxVisibleGaussianSplats = activeGaussianSplats;
            if (m_GaussianSplatSettings.lodBudgetEnabled() && m_GaussianSplatSettings.lodBudget > 0u)
            {
                maxVisibleGaussianSplats = std::min(maxVisibleGaussianSplats, m_GaussianSplatSettings.lodBudget);
            }

            gaussianStats.drawRecords    = static_cast<uint32_t>(m_GpuSceneViewBack.generalGaussianSplatDraws.size());
            gaussianStats.preparedSplats = activeGaussianSplats;
            gaussianStats.maxVisibleSplatCap = maxVisibleGaussianSplats;
            m_GaussianSplatStats             = gaussianStats;

            m_GpuSceneViewBack.setGeneralGaussianSplatCaps(gaussianStats.drawRecords,
                                                           packedGaussianSources,
                                                           selectedSourceCapacity,
                                                           activeGaussianSplats,
                                                           maxVisibleGaussianSplats,
                                                           gaussianDirectPrefix);
            applyGaussianSplatFoveatedClodSettings(m_GpuSceneViewBack, m_GaussianSplatSettings);
            m_GpuSceneViewBack.generalGaussianSplatShBuffer = pool.gaussianStorage.shBuffer;
            m_GpuSceneViewBack.ensureGeneralGaussianSplatBuffers(rd);

            if (!m_GpuSceneViewBack.generalGaussianSplatDraws.empty() &&
                m_GpuSceneViewBack.generalGaussianSplatDrawBuffer)
            {
                cb.update(*m_GpuSceneViewBack.generalGaussianSplatDrawBuffer,
                          0,
                          static_cast<uint64_t>(m_GpuSceneViewBack.generalGaussianSplatDraws.size()) *
                              sizeof(resource::GpuGeneralGaussianSplatDrawRecord),
                          m_GpuSceneViewBack.generalGaussianSplatDraws.data());
            }

            if (!m_GpuSceneViewBack.generalGaussianSplatPackedSources.empty() &&
                m_GpuSceneViewBack.generalGaussianSplatPackedSourceBuffer)
            {
                cb.update(*m_GpuSceneViewBack.generalGaussianSplatPackedSourceBuffer,
                          0,
                          static_cast<uint64_t>(m_GpuSceneViewBack.generalGaussianSplatPackedSources.size()) *
                              sizeof(resource::GpuGeneralGaussianSplatPackedSource),
                          m_GpuSceneViewBack.generalGaussianSplatPackedSources.data());
            }

            if (!m_GpuSceneViewBack.generalGaussianSplatSelectedSources.empty() &&
                m_GpuSceneViewBack.generalGaussianSplatSelectedSourceBuffer)
            {
                RuntimeProfiler::Scope scope {m_RuntimeProfiler, "GaussianLOD::UploadSelected"};
                cb.update(*m_GpuSceneViewBack.generalGaussianSplatSelectedSourceBuffer,
                          0,
                          static_cast<uint64_t>(m_GpuSceneViewBack.generalGaussianSplatSelectedSources.size()) *
                              sizeof(resource::GpuGeneralGaussianSplatSelectedSource),
                          m_GpuSceneViewBack.generalGaussianSplatSelectedSources.data());
            }

            if (m_GpuSceneViewBack.generalGaussianSplatVisibleCountBuffer)
            {
                const uint32_t zero = 0u;
                cb.update(*m_GpuSceneViewBack.generalGaussianSplatVisibleCountBuffer, 0, sizeof(uint32_t), &zero);
            }

            if (m_GpuSceneViewBack.generalGaussianSplatDispatchArgsBuffer)
            {
                const uint32_t zeroArgs[4] = {0u, 1u, 1u, 0u};
                cb.update(*m_GpuSceneViewBack.generalGaussianSplatDispatchArgsBuffer, 0, sizeof(zeroArgs), zeroArgs);
            }

            resetGaussianSplatIndirectBuffers(rd, m_GpuSceneViewBack);

            m_RenderWorldBack.gpuSceneDatabase = &m_GpuSceneDatabaseBack;
            m_RenderWorldBack.gpuSceneView     = &m_GpuSceneViewBack;
        }
        else
        {
            // Reuse previous snapshot when neither cooked world nor resource pool changed.
            m_RenderWorldBack.gpuSceneDatabase = &m_GpuSceneDatabaseFront;
            m_RenderWorldBack.gpuSceneView     = &m_GpuSceneViewFront;
        }

        if (!gpuSceneDirty && gaussianSelectionDirty)
        {
            RuntimeProfiler::Scope scope {m_RuntimeProfiler, "GpuScene::gaussian_lod_selection"};
            auto&                  gpuSceneView = m_GpuSceneViewFront;

            uint32_t selectedSourceCapacity =
                static_cast<uint32_t>(gpuSceneView.generalGaussianSplatSelectedSources.size());
            uint32_t   activeGaussianSplats  = 0u;
            bool       uploadSelectedSources = false;
            const bool gaussianDirectPrefix  = gpuSceneView.generalGaussianSplatDirectPrefix;
            gaussianStats.directPrefix       = gaussianDirectPrefix;
            if (!gaussianDirectPrefix && selectedSourceCapacity < maxGeneralGaussianSplatPoints)
            {
                rebuildGaussianSplatOrderedClodPrefixSources(
                    gpuSceneView, m_RenderWorldBack.gaussianSplats, pool, m_RuntimeProfiler);
                selectedSourceCapacity = static_cast<uint32_t>(gpuSceneView.generalGaussianSplatSelectedSources.size());
                uploadSelectedSources  = true;
            }

            const uint32_t activeBudgetSourceCount =
                gaussianDirectPrefix ? static_cast<uint32_t>(gpuSceneView.generalGaussianSplatPackedSources.size()) :
                                       selectedSourceCapacity;
            activeGaussianSplats =
                std::min(activeBudgetSourceCount,
                         effectiveGaussianLodBudget(m_GaussianSplatSettings, maxGeneralGaussianSplatPoints));
            gaussianStats.lodSelectedRawSplats = activeGaussianSplats;

            uint32_t maxVisibleGaussianSplats = activeGaussianSplats;
            if (m_GaussianSplatSettings.lodBudgetEnabled() && m_GaussianSplatSettings.lodBudget > 0u)
            {
                maxVisibleGaussianSplats = std::min(maxVisibleGaussianSplats, m_GaussianSplatSettings.lodBudget);
            }

            gaussianStats.drawRecords        = static_cast<uint32_t>(gpuSceneView.generalGaussianSplatDraws.size());
            gaussianStats.preparedSplats     = activeGaussianSplats;
            gaussianStats.maxVisibleSplatCap = maxVisibleGaussianSplats;
            m_GaussianSplatStats             = gaussianStats;

            gpuSceneView.setGeneralGaussianSplatCaps(
                gaussianStats.drawRecords,
                static_cast<uint32_t>(gpuSceneView.generalGaussianSplatPackedSources.size()),
                selectedSourceCapacity,
                activeGaussianSplats,
                maxVisibleGaussianSplats,
                gaussianDirectPrefix);
            applyGaussianSplatFoveatedClodSettings(gpuSceneView, m_GaussianSplatSettings);
            gpuSceneView.generalGaussianSplatShBuffer = pool.gaussianStorage.shBuffer;
            gpuSceneView.ensureGeneralGaussianSplatBuffers(rd);

            if (uploadSelectedSources && !gpuSceneView.generalGaussianSplatSelectedSources.empty() &&
                gpuSceneView.generalGaussianSplatSelectedSourceBuffer)
            {
                RuntimeProfiler::Scope scope {m_RuntimeProfiler, "GaussianLOD::UploadSelected"};
                cb.update(*gpuSceneView.generalGaussianSplatSelectedSourceBuffer,
                          0,
                          static_cast<uint64_t>(gpuSceneView.generalGaussianSplatSelectedSources.size()) *
                              sizeof(resource::GpuGeneralGaussianSplatSelectedSource),
                          gpuSceneView.generalGaussianSplatSelectedSources.data());
            }

            if (gpuSceneView.generalGaussianSplatVisibleCountBuffer)
            {
                const uint32_t zero = 0u;
                cb.update(*gpuSceneView.generalGaussianSplatVisibleCountBuffer, 0, sizeof(uint32_t), &zero);
            }

            if (gpuSceneView.generalGaussianSplatDispatchArgsBuffer)
            {
                const uint32_t zeroArgs[4] = {0u, 1u, 1u, 0u};
                cb.update(*gpuSceneView.generalGaussianSplatDispatchArgsBuffer, 0, sizeof(zeroArgs), zeroArgs);
            }

            resetGaussianSplatIndirectBuffers(rd, gpuSceneView);
        }

        m_GpuSceneDirtyTracker.markBuilt(m_RenderWorldBack, resourceRevision, m_EnableGpuDrivenMeshletPipeline);
        if (gpuSceneDirty || gaussianSelectionDirty)
        {
            m_AppliedGaussianSplatSettings = m_GaussianSplatSettings;
        }

        std::swap(m_RenderWorldFront, m_RenderWorldBack);
        if (gpuSceneTopologyDirty)
        {
            std::swap(m_GpuSceneDatabaseFront, m_GpuSceneDatabaseBack);
            std::swap(m_GpuSceneViewFront, m_GpuSceneViewBack);
        }
        m_RenderWorldFront.gpuSceneDatabase = &m_GpuSceneDatabaseFront;
        m_RenderWorldFront.gpuSceneView     = &m_GpuSceneViewFront;
        m_RenderWorldBack.gpuSceneDatabase  = &m_GpuSceneDatabaseBack;
        m_RenderWorldBack.gpuSceneView      = &m_GpuSceneViewBack;

        constexpr uint64_t kOverrideRenderWorldReleaseDelayFrames = 4u;
        std::size_t        overrideOut                            = 0;
        for (auto& slot : m_OverrideRenderWorlds)
        {
            if (m_FrameCounter > slot.lastTouchedFrame + kOverrideRenderWorldReleaseDelayFrames)
            {
                slot = {};
            }
            else
            {
                m_OverrideRenderWorlds[overrideOut++] = std::move(slot);
            }
        }
        m_OverrideRenderWorlds.resize(overrideOut);

        for (const auto& cam : cams)
        {
            if (!cam.worldOverride)
                continue;
            auto slotIt =
                std::find_if(m_OverrideRenderWorlds.begin(),
                             m_OverrideRenderWorlds.end(),
                             [&](const OverrideRenderWorldSlot& slot) { return slot.world == cam.worldOverride; });
            if (slotIt == m_OverrideRenderWorlds.end())
            {
                slotIt        = m_OverrideRenderWorlds.insert(m_OverrideRenderWorlds.end(), OverrideRenderWorldSlot {});
                slotIt->world = cam.worldOverride;
            }
            auto& slot            = *slotIt;
            slot.lastTouchedFrame = m_FrameCounter;
            {
                RuntimeProfiler::Scope scope {m_RuntimeProfiler, "RenderWorldCooker::cook_override"};
                cooker.cook(*slot.world,
                            assetService,
                            gpuResourceService,
                            rd,
                            m_GeometryFactory,
                            slot.renderWorld,
                            renderTimeSeconds);
            }
            slot.renderWorld.frameIndex = m_FrameCounter;
            buildCpuDrivenGpuSceneForRenderWorld(
                slot.renderWorld, slot.gpuSceneDatabase, slot.gpuSceneView, pool, rd, cb);
        }

        m_FrameResources.beginFrame(m_FrameCounter);
        {
            ImmediateResourceUploader frameUploader {m_FrameResources, rd};
            prepareFrameData(frameUploader, m_PreparedFrameData, m_FrameCounter, renderTimeSeconds, 0.0f);
        }

        ++m_FrameCounter;

        std::vector<size_t> cameraOrder(cams.size());
        std::iota(cameraOrder.begin(), cameraOrder.end(), 0u);
        std::stable_sort(cameraOrder.begin(), cameraOrder.end(), [&cams](size_t a, size_t b) {
            return cams[a].priority < cams[b].priority;
        });

        const bool supportsMultiview =
            HasFlagValues(rd.getFeatureReport().flags, rhi::RenderDeviceFeatureReportFlagBits::eMultiview);
        const auto                        xrEyeViews               = backendService.xrEyeViews();
        bool                              skipRemainingStereoViews = false;
        std::unordered_set<rhi::Texture*> clearedTargetsThisFrame;
        m_RuntimeProfiler.setGpuScopeCpuFallback(rd.getBackendApi() == rhi::RenderBackendApi::eWebGPU);

        if (!profilerCaptureEnabled)
        {
            m_RuntimeProfiler.setGpuScopeCallbacks({}, {}, {});
            rhi::setBuiltinProfilerGpuScopeCallbacks({}, {});
        }
        else if (isTrackyGpuProfilerEnabled())
        {
            m_RuntimeProfiler.setGpuScopeCallbacks(
                []() { return uint64_t {0}; }, [](const uint64_t) {}, [](const uint64_t) { return -1.0; });
            rhi::setBuiltinProfilerGpuScopeCallbacks(
                [](const rhi::BuiltinProfilerGpuScopeContext& ctx) { g_CurrentBuiltinProfilerGpuScopeContext = ctx; },
                [this](const rhi::BuiltinProfilerGpuScopeContext& ctx, const char* label) {
                    g_CurrentBuiltinProfilerGpuScopeContext = ctx;
                    (void)m_RuntimeProfiler.beginGpuScope(label ? label : "GPU Scope");
                },
                [this](const rhi::BuiltinProfilerGpuScopeContext& ctx) {
                    g_CurrentBuiltinProfilerGpuScopeContext = ctx;
                    m_RuntimeProfiler.endGpuScope();
                });
        }
        else
        {
            m_RuntimeProfiler.setGpuScopeCallbacks(
                [this, &rd, &cb]() {
                    if (g_CurrentBuiltinProfilerGpuScopeContext.commandBufferHandle == 0)
                        return uint64_t {0};

                    if (rd.getBackendApi() == rhi::RenderBackendApi::eWebGPU)
                    {
                        return uint64_t {0};
                    }

                    // WebGPU compute encoders are kept open lazily. At a framegraph pass boundary the next
                    // top-level scope may still observe the previous compute pass as active, which would
                    // incorrectly suppress or mis-attribute the new pass timing.
                    if (rd.getBackendApi() == rhi::RenderBackendApi::eWebGPU &&
                        g_CurrentBuiltinProfilerGpuScopeContext.renderPassEncoderHandle == 0 &&
                        g_CurrentBuiltinProfilerGpuScopeContext.computePassEncoderHandle != 0 &&
                        m_RuntimeProfiler.gpuScopeDepth() <= 1)
                    {
                        rhi::WebGPUCommandBufferAccess::closeActiveComputePassForProfilingBoundary(cb);
                        g_CurrentBuiltinProfilerGpuScopeContext.renderPassEncoderHandle =
                            cb.getCurrentRenderPassEncoderHandle();
                        g_CurrentBuiltinProfilerGpuScopeContext.computePassEncoderHandle =
                            cb.getCurrentComputePassEncoderHandle();
                    }

                    // WebGPU fallback timestamps are pass-bound; ignore nested scopes inside an active pass.
                    if (rd.getBackendApi() == rhi::RenderBackendApi::eWebGPU &&
                        (g_CurrentBuiltinProfilerGpuScopeContext.renderPassEncoderHandle != 0 ||
                         g_CurrentBuiltinProfilerGpuScopeContext.computePassEncoderHandle != 0))
                    {
                        return uint64_t {0};
                    }
                    return rd.beginScopeGpuQuery(g_CurrentBuiltinProfilerGpuScopeContext.commandBufferHandle);
                },
                [&rd](const uint64_t token) {
                    if (g_CurrentBuiltinProfilerGpuScopeContext.commandBufferHandle == 0 || token == 0)
                        return;
                    if (rd.getBackendApi() == rhi::RenderBackendApi::eWebGPU)
                        return;
                    rd.endScopeGpuQuery(g_CurrentBuiltinProfilerGpuScopeContext.commandBufferHandle, token);
                },
                [&rd](const uint64_t token) {
                    if (rd.getBackendApi() == rhi::RenderBackendApi::eWebGPU)
                        return -1.0;
                    return rd.consumeScopeGpuMs(token);
                });
            rhi::setBuiltinProfilerGpuScopeCallbacks(
                [](const rhi::BuiltinProfilerGpuScopeContext& ctx) { g_CurrentBuiltinProfilerGpuScopeContext = ctx; },
                [this](const rhi::BuiltinProfilerGpuScopeContext& ctx, const char* label) {
                    g_CurrentBuiltinProfilerGpuScopeContext = ctx;
                    (void)m_RuntimeProfiler.beginGpuScope(label ? label : "GPU Scope");
                },
                [this](const rhi::BuiltinProfilerGpuScopeContext& ctx) {
                    g_CurrentBuiltinProfilerGpuScopeContext = ctx;
                    m_RuntimeProfiler.endGpuScope();
                });
        }

        // TODO: TimeSystem, for now use 0
        const fsec dt {0};
        static_cast<void>(dt);

        for (const size_t cameraIdx : cameraOrder)
        {
            RuntimeProfiler::Scope scopeCamera {m_RuntimeProfiler, "RenderCamera::execute"};
            const auto&            cam = cams[cameraIdx];

            if (skipRemainingStereoViews && cam.isXRView && !cam.isXRPrimaryView)
                continue;
            if (!cam.isXRView || cam.isXRPrimaryView)
                skipRemainingStereoViews = false;

            FrameGraph             fg {};
            FrameGraphBlackboard   bb {};
            FrameGraphDataRegistry dataRegistry {};

            const bool canUseXrMultiview = supportsMultiview && cam.isXRView && cam.isXRPrimaryView &&
                                           cam.viewCount == 2u && !xrEyeViews.empty() && xrEyeViews[0].stereoTarget;

            rhi::Texture* target =
                canUseXrMultiview ? xrEyeViews[0].stereoTarget : (cam.target ? cam.target : &defaultTarget);
            if (!target)
                continue;

            const bool isBackbufferTarget = !cam.isXRView && cam.target == nullptr && target == &defaultTarget;
            const bool useWindowContentArea =
                isBackbufferTarget && window.platformType() == os::Window::PlatformType::eAndroidNativeWindow;
            const rhi::Rect2D  renderArea  = useWindowContentArea ?
                                                 window.getContentArea() :
                                                 rhi::Rect2D {.offset = {0, 0}, .extent = target->getExtent()};
            const RenderCamera viewCamera  = cameraForRenderExtent(cam, renderArea.extent);
            RenderWorld*       renderWorld = &m_RenderWorldFront;
            if (cam.worldOverride)
            {
                const auto slotIt =
                    std::find_if(m_OverrideRenderWorlds.begin(),
                                 m_OverrideRenderWorlds.end(),
                                 [&](const OverrideRenderWorldSlot& slot) { return slot.world == cam.worldOverride; });
                if (slotIt != m_OverrideRenderWorlds.end())
                    renderWorld = &slotIt->renderWorld;
            }

            RenderView view {
                .renderWorld          = renderWorld,
                .camera               = &viewCamera,
                .target               = target,
                .extent               = renderArea.extent,
                .clearValue           = viewCamera.clearValue,
                .stereoMode           = canUseXrMultiview ?
                                            StereoRenderMode::eSingleGraphStereo :
                                            (cam.isXRView ? StereoRenderMode::ePerEyeFallback : StereoRenderMode::eMono),
                .enableMultiview      = canUseXrMultiview,
                .multiviewMask        = canUseXrMultiview ? 0x3u : 0u,
                .multiviewCameras     = {&viewCamera, nullptr},
                .multiviewCameraCount = canUseXrMultiview ? 2u : 0u,
                .xrStereoTarget       = canUseXrMultiview ? xrEyeViews[0].stereoTarget : nullptr,
                .xrEyeTargets         = {xrEyeViews.size() > 0u ? xrEyeViews[0].target : nullptr,
                                         xrEyeViews.size() > 1u ? xrEyeViews[1].target : nullptr},
                .gpuSceneDatabase     = renderWorld->gpuSceneDatabase,
                .gpuSceneView         = renderWorld->gpuSceneView,
            };

            if (canUseXrMultiview)
            {
                const auto secondEyeIt = std::find_if(cameraOrder.begin(), cameraOrder.end(), [&](size_t idx) {
                    return cams[idx].isXRView && !cams[idx].isXRPrimaryView && cams[idx].viewCount == cam.viewCount;
                });
                if (secondEyeIt != cameraOrder.end())
                    view.multiviewCameras[1] = &cams[*secondEyeIt];
            }

            rhi::FramebufferInfo fbInfo {
                .area             = renderArea,
                .layers           = canUseXrMultiview ? 2u : 1u,
                .viewMask         = canUseXrMultiview ? 0x3u : 0u,
                .colorAttachments = {rhi::AttachmentInfo {.target = target, .clearValue = viewCamera.clearValue}},
            };

            ViewRenderData viewData {
                .view            = view,
                .framebufferInfo = fbInfo,
            };

            // Fallback clear for camera targets.
            // This guarantees a deterministic background even when renderer contributes no color pass
            // (e.g. pure ImGui examples or empty editor view render targets).
            if (clearedTargetsThisFrame.insert(target).second)
            {
                clearColorTarget(
                    cb, *target, renderArea, viewCamera.clearValue, canUseXrMultiview, canUseXrMultiview ? 0x3u : 0u);
            }

            auto renderer = resolveRenderer(cam);
            if (!renderer)
            {
                if (static_cast<bool>(target->getUsageFlags() & rhi::ImageUsage::eSampled))
                    rhi::prepareForReading(cb, *target);
                continue;
            }

            const bool useFrameGraph = renderer->usesFrameGraph();

            {
                ImmediateResourceUploader immediateUploader {m_FrameResources, rd};
                prepareCameraData(immediateUploader, viewData, renderArea.extent, viewCamera, rd.getBackendApi());
            }

            ImmediateRenderContext immediateCtx {
                .cb          = cb,
                .rd          = rd,
                .frame       = m_PreparedFrameData,
                .viewData    = viewData,
                .resourceSet = {},
            };

            rhi::prepareForAttachment(cb, *target, false);
            renderer->render(immediateCtx);
            if (useFrameGraph)
            {
                FrameGraphResourceUploader fgUploader {fg};
                prepareFrameData(fgUploader,
                                 m_PreparedFrameData,
                                 m_RenderWorldFront.frameIndex,
                                 static_cast<float>(m_RenderWorldFront.frameIndex) / 60.0f,
                                 0.0f);
                prepareCameraData(fgUploader, viewData, renderArea.extent, viewCamera, rd.getBackendApi());
                bb.add<FrameData>(m_PreparedFrameData.frameData);
                bb.add<CameraData>(viewData.cameraData);
                bb.add<StereoViewData>(viewData.stereoViewData);
            }

            if (useFrameGraph)
            {
                RuntimeProfiler::Scope scopeFrameGraphBuild {m_RuntimeProfiler, "FrameGraph::build"};
                FrameGraphBuildContext buildCtx {
                    .fg       = fg,
                    .bb       = bb,
                    .rd       = rd,
                    .data     = dataRegistry,
                    .frame    = m_PreparedFrameData,
                    .viewData = viewData,
                };

                // This sets up the frame graph using a feature renderer or a custom graph-aware renderer.
                rhi::prepareForAttachment(cb, *target, false);
                renderer->buildFrameGraph(buildCtx);

                std::ostringstream runtimeDot;
                fg.debugOutput(runtimeDot, graphviz::Writer {});

                addFrameGraphTextureCapturePasses(buildCtx, viewCamera);
                fg.compile();

                {
                    std::ostringstream       snapshot;
                    FrameGraphSnapshotWriter snapshotWriter {cam.name, cam.rendererKey, runtimeDot.str()};
                    fg.debugOutput(snapshot, snapshotWriter);
                    m_LastFrameGraphSnapshot += snapshot.str();
                    m_LastFrameGraphSnapshot += "\n";
                }

#ifndef NDEBUG
                {
                    const std::filesystem::path debugRoot = !ctx().config.writableRoot.empty() ?
                                                                std::filesystem::path(ctx().config.writableRoot) :
                                                                vbase::executable_dir();
                    const std::filesystem::path debugPath = debugRoot / "framegraph.jsonl";
                    std::ofstream               ofs(debugPath);
                    if (ofs.is_open())
                    {
                        ofs << m_LastFrameGraphSnapshot;
                    }
                    else
                    {
                        VULTRA_CORE_WARN("[RenderSystem] Failed to write framegraph snapshot file: {}",
                                         debugPath.generic_string());
                    }
                }
#endif

                viewData.framebufferInfo = std::nullopt; // Clear framebuffer info for execution phase, will be set by
                                                         // FrameGraphTexture preRead callback if needed.
                FrameGraphExecContext frameGraphExecCtx {
                    .cb          = cb,
                    .rd          = rd,
                    .frame       = m_PreparedFrameData,
                    .viewData    = viewData,
                    .resourceSet = {},
                    .ext         = {.builtinShaderLib        = &shaderService.builtinLibrary(),
                                    .builtinHighendShaderLib = &shaderService.builtinLibrary(rhi::ShaderProfile::eHighend),
                                    .builtinCompatibilityShaderLib =
                                        &shaderService.builtinLibrary(rhi::ShaderProfile::eCompatibility),
                                    .samplers = m_Samplers},
                };

                {
                    RuntimeProfiler::Scope scopeFrameGraphExec {m_RuntimeProfiler, "FrameGraph::execute"};
                    FG_GPU_ZONE(cb);
                    fg.execute(&frameGraphExecCtx, m_TransientResources.get());
                }
            }

            if (canUseXrMultiview)
                skipRemainingStereoViews = true;

            // Optional ImGui rendering per non-XR camera
            if (imguiService && !cam.isXRView && cam.renderImGui)
            {
                imguiService->begin();
                renderer->onImGui();
                imguiService->end();

                rhi::prepareForAttachment(cb, *target, false);
                imguiService->render(cb, fbInfo);
            }

            if (static_cast<bool>(target->getUsageFlags() & rhi::ImageUsage::eSampled))
                rhi::prepareForReading(cb, *target);
        }

        {
            for (auto it = m_FrameGraphDebugTextureSlots.begin(); it != m_FrameGraphDebugTextureSlots.end();)
            {
                if (it->second.lastTouchedFrame != m_FrameCounter)
                {
                    it->second.lastTouchedFrame = m_FrameCounter;
                    m_RetiredFrameGraphDebugTextureSlots.push_back(std::move(it->second));
                    it = m_FrameGraphDebugTextureSlots.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }

        if (imguiService && backendService.isXREnabled() && backendService.isXRMirrorEnabled() && !xrEyeViews.empty())
        {
            struct alignas(16) MirrorPreviewPushConstants
            {
                glm::ivec4 channelMask {1, 1, 1, 1};
                int        gammaCorrect {0};
                int        previewMode {0};
                float      depthNear {0.1f};
                float      depthFar {1000.0f};
                float      clampMin {0.0f};
                float      clampMax {1.0f};
            };

            for (const auto& eyeView : xrEyeViews)
            {
                if (!eyeView.target || !eyeView.mirrorTarget)
                    continue;

                if (eyeView.stereoTarget)
                {
                    eyeView.target->setBarrierState(eyeView.stereoTarget->getLastBarrierScope(),
                                                    eyeView.stereoTarget->getImageLayout());
                }

                rhi::prepareForReading(cb, *eyeView.target);
                if (m_BuiltinRenderSettings.xrMirrorGammaCorrect)
                {
                    auto* pipeline =
                        getFrameGraphTexturePreviewPipeline(rd,
                                                            shaderService.builtinLibrary(rhi::ShaderProfile::eGeneral),
                                                            eyeView.mirrorTarget->getPixelFormat());
                    const auto samplerIt = m_Samplers.find("linear");
                    if (pipeline && samplerIt != m_Samplers.end())
                    {
                        const auto descriptorSet = cb.createDescriptorSetBuilder()
                                                       .bind(0,
                                                             rhi::bindings::CombinedImageSampler {
                                                                 .texture     = eyeView.target,
                                                                 .imageAspect = rhi::ImageAspect::eColor,
                                                                 .sampler     = samplerIt->second,
                                                             })
                                                       .build(pipeline->getDescriptorSetLayout(3));
                        const MirrorPreviewPushConstants pc {
                            .channelMask  = glm::ivec4(1, 1, 1, 0),
                            .gammaCorrect = 1,
                            .previewMode  = 0,
                            .depthNear    = 0.1f,
                            .depthFar     = 1000.0f,
                            .clampMin     = 0.0f,
                            .clampMax     = 1.0f,
                        };
                        rhi::prepareForAttachment(cb, *eyeView.mirrorTarget, false);
                        cb.bindPipeline(*pipeline)
                            .bindDescriptorSet(3, descriptorSet)
                            .pushConstants(rhi::ShaderStages::eFragment, 0, &pc)
                            .beginRendering({
                                .area             = {.extent = eyeView.mirrorTarget->getExtent()},
                                .colorAttachments = {rhi::AttachmentInfo {.target = eyeView.mirrorTarget}},
                            })
                            .drawFullScreenTriangle()
                            .endRendering();
                    }
                    else
                    {
                        cb.blit(*eyeView.target, *eyeView.mirrorTarget, rhi::TexelFilter::eLinear);
                    }
                }
                else
                {
                    cb.blit(*eyeView.target, *eyeView.mirrorTarget, rhi::TexelFilter::eLinear);
                }
                rhi::prepareForReading(cb, *eyeView.target);

                if (eyeView.stereoTarget)
                {
                    eyeView.stereoTarget->setBarrierState(eyeView.target->getLastBarrierScope(),
                                                          eyeView.target->getImageLayout());
                }
            }

            imguiService->begin();

            std::unordered_set<Renderer*> imguiRenderers;
            for (const size_t cameraIdx : cameraOrder)
            {
                const auto& cam = cams[cameraIdx];
                if (cam.isXRView || !cam.renderImGui)
                    continue;

                auto renderer = resolveRenderer(cam);
                if (!renderer || imguiRenderers.contains(renderer.get()))
                    continue;
                imguiRenderers.insert(renderer.get());
                renderer->onImGui();
            }

            imguiService->end();

            for (const auto& xrEyeView : xrEyeViews)
            {
                if (xrEyeView.mirrorTarget)
                    rhi::prepareForReading(cb, *xrEyeView.mirrorTarget);
            }

            const rhi::Rect2D imguiArea = window.platformType() == os::Window::PlatformType::eAndroidNativeWindow ?
                                              window.getContentArea() :
                                              rhi::Rect2D {.offset = {0, 0}, .extent = defaultTarget.getExtent()};

            rhi::FramebufferInfo imguiFbInfo {
                .area             = imguiArea,
                .colorAttachments = {rhi::AttachmentInfo {.target = &defaultTarget}},
            };

            rhi::prepareForAttachment(cb, defaultTarget, false);
            imguiService->render(cb, imguiFbInfo);
        }

        // Stop issuing begin/end scope queries after rendering submission building is done,
        // but keep resolve callback alive so endFrame can harvest ready GPU samples.
        if (gpuTimingEnabled)
        {
            m_RuntimeProfiler.setGpuScopeCallbacks([]() { return uint64_t {0}; },
                                                   [](const uint64_t) {},
                                                   [&rd](const uint64_t token) { return rd.consumeScopeGpuMs(token); });
        }

        m_TransientResources->update();
        if (gpuTimingEnabled)
        {
            rd.endFrameGpuQuery(cb);
        }
        backendService.endFrame();

        const auto commandStats = rhi::CommandBuffer::consumeFrameStats();
        m_RuntimeProfiler.setCommandStats(commandStats.drawCalls,
                                          commandStats.dispatchCalls,
                                          commandStats.traceRaysCalls,
                                          commandStats.copyOps,
                                          commandStats.updateOps);
        const auto assetMemoryStats = assetService.memoryStats();
        const auto memoryStats      = rd.getMemoryStats();
        m_RuntimeProfiler.setMemoryStats(assetMemoryStats.cpuCacheBytes,
                                         memoryStats.cpuCacheBytes,
                                         memoryStats.gpuDeviceLocalBytes,
                                         memoryStats.gpuHostVisibleBytes);
        const double gpuFrameMs = gpuTimingEnabled ? rd.consumeGpuFrameMs() : -1.0;
        m_RuntimeProfiler.setGpuFrameMs(gpuFrameMs);
        const auto renderFrameCpuEnd = std::chrono::steady_clock::now();
        m_RuntimeProfiler.setCpuRenderMs(
            std::chrono::duration<double, std::milli>(renderFrameCpuEnd - renderFrameCpuStart).count());
        m_RuntimeProfiler.endFrame();
        updateGaussianSplatFoveatedBudgetController(m_GaussianSplatSettings, gpuFrameMs);
        rhi::setBuiltinProfilerGpuScopeCallbacks({}, {});
        m_RuntimeProfiler.setGpuScopeCallbacks({}, {}, {});
    }

    void RenderSystem::onPreRender() { m_SkipRender = false; }

    void RenderSystem::onRender() { renderFrame(); }

    void RenderSystem::onPostRender()
    {
        auto* imguiService = ctx().services.tryGet<IImGuiService>();
        if (imguiService)
            imguiService->postRender();
    }

    void RenderSystem::onPresent()
    {
        if (m_SkipRender)
            return;

        auto& backendService = ctx().services.require<IRenderBackendService>();
        backendService.present();

        if (auto* frameDebuggerService = ctx().services.tryGet<IFrameDebuggerService>())
            frameDebuggerService->captureEnd();
    }
} // namespace vultra
