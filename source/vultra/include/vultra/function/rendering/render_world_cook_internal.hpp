#pragma once

// Internal interface between render_system.cpp and render_world_cook.cpp.
//
// render_world_cook.cpp owns the CPU-driven render-world / GPU-scene cooking helpers (entity
// renderability, material-index remap, skin-palette lookup, GPU-scene build, gaussian-splat
// indirect-buffer reset). Only the entry points below are called from render_system.cpp's
// members; the rest (buildCpuDrivenGaussianSplatsForRenderWorld, the single-buffer reset) are
// file-local. Definitions live in render_world_cook.cpp under namespace vultra::rsdetail.

#include <entt/entity/fwd.hpp> // entt::registry, entt::entity

#include <cstdint>

namespace vultra
{
    class World;
    struct RenderInstance;
    struct RenderWorld;
    struct SkinPaletteComponent;

    namespace resource
    {
        struct GpuMesh;
        struct GpuSceneView;
        struct GpuSceneDatabase;
        struct GpuResourcePool;
    } // namespace resource

    namespace rhi
    {
        class RenderDevice;
        class CommandBuffer;
    } // namespace rhi

    namespace rsdetail
    {
        [[nodiscard]] uint32_t
        remapMaterialIndex(const RenderInstance& instance, const resource::GpuMesh& mesh, uint32_t materialIndex);

        [[nodiscard]] const SkinPaletteComponent*
        findSkinPaletteForMesh(entt::registry& reg, entt::entity entity, const resource::GpuMesh& mesh);

        [[nodiscard]] bool isEntityRenderable(const World& world, const entt::registry& reg, entt::entity entity);

        void resetGaussianSplatIndirectBuffers(rhi::RenderDevice& rd, resource::GpuSceneView& gpuSceneView);

        void buildCpuDrivenGpuSceneForRenderWorld(RenderWorld&                     renderWorld,
                                                  resource::GpuSceneDatabase&      gpuSceneDatabase,
                                                  resource::GpuSceneView&          gpuSceneView,
                                                  const resource::GpuResourcePool& pool,
                                                  rhi::RenderDevice&               rd,
                                                  rhi::CommandBuffer&              cb);

        [[nodiscard]] bool hasSkinMatrices(const RenderWorld& renderWorld);

        [[nodiscard]] bool refreshSkinMatricesForExistingGpuScene(RenderWorld&                renderWorld,
                                                                  resource::GpuSceneDatabase& gpuSceneDatabase);
    } // namespace rsdetail
} // namespace vultra
