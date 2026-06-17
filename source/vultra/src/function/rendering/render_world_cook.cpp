// Render-world / GPU-scene cooking translation unit.
//
// Split out of render_system.cpp: the
// CPU-driven render-world -> GPU-scene build helpers (entity renderability, material-index
// remap, skin-palette lookup, gaussian-splat CPU build, GPU-scene build) plus the
// gaussian-splat indirect-buffer reset helpers. Moving the two reset helpers together with
// this block leaves no back-edges. Entry points called from render_system.cpp are declared in
// render_world_cook_internal.hpp (namespace vultra::rsdetail). Pure code move.

#include "vultra/function/rendering/render_system.hpp"
#include "vultra/function/material_graph/material_graph_compiler.hpp"
#include "vultra/function/rendering/render_system_internal.hpp" // material cooking moved to material_cook.cpp

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
#include "vultra/function/rendering/render_world_cook_internal.hpp"

namespace vultra
{
    namespace rsdetail
    {
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

        [[nodiscard]] const SkinPaletteComponent* findSkinPaletteForMesh(entt::registry&          reg,
                                                                          entt::entity            entity,
                                                                          const resource::GpuMesh& mesh)
        {
            if (!mesh.hasSkin || !mesh.skeleton.valid())
                return nullptr;

            for (auto cursor = entity; cursor != entt::null && reg.valid(cursor);)
            {
                if (const auto* palette = reg.try_get<SkinPaletteComponent>(cursor))
                {
                    if (palette->skeleton == mesh.skeleton && palette->matrices.size() >= mesh.inverseBindPoses.size())
                        return palette;
                }

                const auto* hierarchy = reg.try_get<HierarchyComponent>(cursor);
                cursor = hierarchy ? hierarchy->parent : entt::null;
            }
            return nullptr;
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


        void buildCpuDrivenGaussianSplatsForRenderWorld(RenderWorld&                     renderWorld,
                                                        resource::GpuSceneView&          gpuSceneView,
                                                        const resource::GpuResourcePool& pool,
                                                        rhi::RenderDevice&               rd,
                                                        rhi::CommandBuffer&              cb)
        {
            uint32_t maxGeneralGaussianSplatSourceCount = 0u;
            for (const auto& splatInst : renderWorld.gaussianSplats)
            {
                if (splatInst.splatIndex >= pool.gaussianSplats.size())
                    continue;
                maxGeneralGaussianSplatSourceCount += pool.gaussianSplats[splatInst.splatIndex].pointCount;
            }

            gpuSceneView.generalGaussianSplatDraws.clear();
            gpuSceneView.generalGaussianSplatPackedSources.clear();
            gpuSceneView.generalGaussianSplatSelectedSources.clear();
            gpuSceneView.generalGaussianSplatDirectPrefix = false;
            gpuSceneView.generalGaussianSplatDraws.reserve(renderWorld.gaussianSplats.size());
            gpuSceneView.generalGaussianSplatPackedSources.reserve(maxGeneralGaussianSplatSourceCount);
            gpuSceneView.generalGaussianSplatSelectedSources.reserve(maxGeneralGaussianSplatSourceCount);

            for (const auto& splatInst : renderWorld.gaussianSplats)
            {
                if (splatInst.splatIndex >= pool.gaussianSplats.size())
                    continue;

                const auto& gpuSplat = pool.gaussianSplats[splatInst.splatIndex];
                if (gpuSplat.pointCount == 0u)
                    continue;

                const uint32_t drawIndex = static_cast<uint32_t>(gpuSceneView.generalGaussianSplatDraws.size());
                const uint32_t pointBase = gpuSplat.pointOffset;
                const uint32_t shBaseStride = std::max(gpuSplat.shRestCoeffCount, 1u);
                const uint32_t rawSourceOffset =
                    static_cast<uint32_t>(gpuSceneView.generalGaussianSplatPackedSources.size());

                resource::GpuGeneralGaussianSplatDrawRecord drawRecord {};
                drawRecord.splatIndex  = splatInst.splatIndex;
                drawRecord.pointOffset = rawSourceOffset;
                drawRecord.pointCount  = gpuSplat.pointCount;
                drawRecord.shDegree    = static_cast<uint32_t>(std::max(gpuSplat.shDegree, 0));
                drawRecord.params0     = glm::vec4 {0.3f, 1.0f, 1.0f, 0.0f};
                drawRecord.model       = splatInst.worldMatrix;

                for (uint32_t localPoint = 0; localPoint < gpuSplat.pointCount; ++localPoint)
                {
                    const uint32_t globalPoint = pointBase + localPoint;
                    resource::GpuGeneralGaussianSplatPackedSource packed {};
                    if (globalPoint < pool.gaussianStorage.cpuCenters.size() &&
                        globalPoint < pool.gaussianStorage.cpuCovariances.size() &&
                        globalPoint < pool.gaussianStorage.cpuColors.size())
                    {
                        const glm::vec4 localCenter = pool.gaussianStorage.cpuCenters[globalPoint];
                        const uint32_t  shOffset    = globalPoint * shBaseStride;
                        const glm::uvec2 sh0        = shOffset < pool.gaussianStorage.cpuSh.size() ?
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

                    const uint32_t sourceIndex = gpuSceneView.pushGeneralGaussianSplatSource(packed);
                    resource::GpuGeneralGaussianSplatSelectedSource selected {};
                    selected.sourceIndex  = sourceIndex;
                    selected.drawIndex    = drawIndex;
                    selected.packedWeight = std::bit_cast<uint32_t>(1.0f);
                    selected.flags        = 0u;
                    gpuSceneView.pushGeneralGaussianSplatSelectedSource(selected);
                }

                gpuSceneView.pushGeneralGaussianSplatDraw(drawRecord);
            }

            const uint32_t drawCount = static_cast<uint32_t>(gpuSceneView.generalGaussianSplatDraws.size());
            const uint32_t sourceCount = static_cast<uint32_t>(gpuSceneView.generalGaussianSplatPackedSources.size());
            const uint32_t selectedCount = static_cast<uint32_t>(gpuSceneView.generalGaussianSplatSelectedSources.size());
            gpuSceneView.setGeneralGaussianSplatCaps(drawCount, sourceCount, selectedCount, selectedCount, selectedCount, false);
            gpuSceneView.resetGeneralGaussianSplatFoveatedClod();
            gpuSceneView.generalGaussianSplatShBuffer = pool.gaussianStorage.shBuffer;
            gpuSceneView.ensureGeneralGaussianSplatBuffers(rd);

            if (!gpuSceneView.generalGaussianSplatDraws.empty() && gpuSceneView.generalGaussianSplatDrawBuffer)
            {
                cb.update(*gpuSceneView.generalGaussianSplatDrawBuffer,
                          0,
                          static_cast<uint64_t>(gpuSceneView.generalGaussianSplatDraws.size()) *
                              sizeof(resource::GpuGeneralGaussianSplatDrawRecord),
                          gpuSceneView.generalGaussianSplatDraws.data());
            }
            if (!gpuSceneView.generalGaussianSplatPackedSources.empty() && gpuSceneView.generalGaussianSplatPackedSourceBuffer)
            {
                cb.update(*gpuSceneView.generalGaussianSplatPackedSourceBuffer,
                          0,
                          static_cast<uint64_t>(gpuSceneView.generalGaussianSplatPackedSources.size()) *
                              sizeof(resource::GpuGeneralGaussianSplatPackedSource),
                          gpuSceneView.generalGaussianSplatPackedSources.data());
            }
            if (!gpuSceneView.generalGaussianSplatSelectedSources.empty() && gpuSceneView.generalGaussianSplatSelectedSourceBuffer)
            {
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

            for (auto& inst : renderWorld.instances)
            {
                const uint32_t        transformIndex = gpuSceneDatabase.pushTransform(inst.worldMatrix);
                inst.skinMatrixOffset = gpuSceneDatabase.pushSkinMatrices(inst.skinMatrices);
                resource::GpuInstance gpuInst {};
                gpuInst.meshIndex      = inst.meshIndex;
                gpuInst.materialIndex  = inst.materialIndex;
                gpuInst.transformIndex = transformIndex;
                gpuInst.flags          = 0;
                gpuInst.entityPickingId = makeEntityPickingId(inst.entity);
                gpuInst.skinMatrixOffset = inst.skinMatrixOffset;
                gpuInst.skinMatrixCount  = inst.skinMatrixCount;
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
                    dr.jointIndicesOffsetBytes = layout.jointIndicesOffsetBytes;
                    dr.jointWeightsOffsetBytes = layout.jointWeightsOffsetBytes;
                    dr.skinMatrixOffset = gpuSceneDatabase.instances[instanceIndex].skinMatrixOffset;
                    dr.skinMatrixCount  = gpuSceneDatabase.instances[instanceIndex].skinMatrixCount;
                    dr.entityPickingId      = makeEntityPickingId(inst.entity);
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
            buildCpuDrivenGaussianSplatsForRenderWorld(renderWorld, gpuSceneView, pool, rd, cb);

            renderWorld.gpuSceneDatabase = &gpuSceneDatabase;
            renderWorld.gpuSceneView     = &gpuSceneView;
        }

        [[nodiscard]] bool hasSkinMatrices(const RenderWorld& renderWorld)
        {
            return std::any_of(renderWorld.instances.begin(), renderWorld.instances.end(), [](const RenderInstance& inst) {
                return !inst.skinMatrices.empty();
            });
        }

        [[nodiscard]] bool refreshSkinMatricesForExistingGpuScene(RenderWorld&                renderWorld,
                                                                  resource::GpuSceneDatabase& gpuSceneDatabase)
        {
            if (renderWorld.instances.size() != gpuSceneDatabase.instances.size())
                return false;

            for (uint32_t instanceIndex = 0; instanceIndex < static_cast<uint32_t>(renderWorld.instances.size());
                 ++instanceIndex)
            {
                auto&       inst    = renderWorld.instances[instanceIndex];
                const auto& gpuInst = gpuSceneDatabase.instances[instanceIndex];

                if (inst.skinMatrices.empty())
                {
                    if (gpuInst.skinMatrixCount != 0u)
                        return false;
                    inst.skinMatrixOffset = std::numeric_limits<uint32_t>::max();
                    inst.skinMatrixCount  = 0u;
                    continue;
                }

                const uint32_t count = static_cast<uint32_t>(inst.skinMatrices.size());
                if (gpuInst.skinMatrixOffset == std::numeric_limits<uint32_t>::max() ||
                    gpuInst.skinMatrixCount != count ||
                    gpuInst.skinMatrixOffset + count > gpuSceneDatabase.skinMatrices.size())
                {
                    return false;
                }

                std::copy(inst.skinMatrices.begin(),
                          inst.skinMatrices.end(),
                          gpuSceneDatabase.skinMatrices.begin() + gpuInst.skinMatrixOffset);
                inst.skinMatrixOffset = gpuInst.skinMatrixOffset;
                inst.skinMatrixCount  = gpuInst.skinMatrixCount;
            }

            return true;
        }
    } // namespace rsdetail
} // namespace vultra
