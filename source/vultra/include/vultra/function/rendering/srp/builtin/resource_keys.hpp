#pragma once

#include "vultra/function/framegraph/framegraph_data_registry.hpp"

#include <vbase/core/hash.hpp>

namespace vultra
{
    // -------- Imported resources --------
    // set = 0, binding = 1
    constexpr FrameGraphResourceKey kResKey_DrawBuffer {.id = vbase::hashLiteral("DrawBuffer")};
    // set = 0, binding = 2
    constexpr FrameGraphResourceKey kResKey_InstanceBuffer {.id = vbase::hashLiteral("InstanceBuffer")};
    // set = 0, binding = 3
    constexpr FrameGraphResourceKey kResKey_MeshTableBuffer {.id = vbase::hashLiteral("MeshTableBuffer")};
    // set = 0, binding = 4
    constexpr FrameGraphResourceKey kResKey_MeshletsBuffer {.id = vbase::hashLiteral("MeshletsBuffer")};
    // set = 0, binding = 5
    constexpr FrameGraphResourceKey kResKey_TransformBuffer {.id = vbase::hashLiteral("TransformBuffer")};
    // set = 0, binding = 6
    constexpr FrameGraphResourceKey kResKey_VisibleMeshletBuffer {.id = vbase::hashLiteral("VisibleMeshletBuffer")};
    // set = 0, binding = 7
    constexpr FrameGraphResourceKey kResKey_VisibleMeshletCountBuffer {
        .id = vbase::hashLiteral("VisibleMeshletCountBuffer")};
    // set = 0, binding = 8
    constexpr FrameGraphResourceKey kResKey_MaterialTableBuffer {.id = vbase::hashLiteral("MaterialTableBuffer")};
    // set = 0, binding = 9
    constexpr FrameGraphResourceKey kResKey_MaterialParametersBuffer {
        .id = vbase::hashLiteral("MaterialParametersBuffer")};
    // set = 0, binding = 10
    constexpr FrameGraphResourceKey kResKey_MeshletVertexBuffer {.id = vbase::hashLiteral("MeshletVertexBuffer")};
    // set = 0, binding = 11
    constexpr FrameGraphResourceKey kResKey_MeshletTriangleBuffer {.id = vbase::hashLiteral("MeshletTriangleBuffer")};
    // set = 0, binding = 12
    constexpr FrameGraphResourceKey kResKey_IndirectBuffer {.id = vbase::hashLiteral("IndirectBuffer")};
    // set = 0, binding = 13
    constexpr FrameGraphResourceKey kResKey_GeneralGaussianSplatDrawBuffer {
        .id = vbase::hashLiteral("GeneralGaussianSplatDrawBuffer")};
    // set = 0, binding = 14
    constexpr FrameGraphResourceKey kResKey_GeneralGaussianSplatPackedSourceBuffer {
        .id = vbase::hashLiteral("GeneralGaussianSplatPackedSourceBuffer")};
    // set = 0, binding = 15
    constexpr FrameGraphResourceKey kResKey_GeneralGaussianSplatVisibleSplatBuffer {
        .id = vbase::hashLiteral("GeneralGaussianSplatVisibleSplatBuffer")};
    // set = 0, binding = 16
    constexpr FrameGraphResourceKey kResKey_GeneralGaussianSplatSortKeyBuffer {
        .id = vbase::hashLiteral("GeneralGaussianSplatSortKeyBuffer")};
    // set = 0, binding = 17
    constexpr FrameGraphResourceKey kResKey_GeneralGaussianSplatSortIndexBuffer {
        .id = vbase::hashLiteral("GeneralGaussianSplatSortIndexBuffer")};
    // set = 0, binding = 18
    constexpr FrameGraphResourceKey kResKey_GeneralGaussianSplatVisibleCountBuffer {
        .id = vbase::hashLiteral("GeneralGaussianSplatVisibleCountBuffer")};
    // set = 0, binding = 19
    constexpr FrameGraphResourceKey kResKey_GeneralGaussianSplatDispatchArgsBuffer {
        .id = vbase::hashLiteral("GeneralGaussianSplatDispatchArgsBuffer")};
    // set = 0, binding = 20
    constexpr FrameGraphResourceKey kResKey_GeneralGaussianSplatIndirectBuffer {
        .id = vbase::hashLiteral("GeneralGaussianSplatIndirectBuffer")};
    // set = 0, binding = 21
    constexpr FrameGraphResourceKey kResKey_GeneralGaussianSplatSortStorageBuffer {
        .id = vbase::hashLiteral("GeneralGaussianSplatSortStorageBuffer")};
    // set = 0, binding = 22
    constexpr FrameGraphResourceKey kResKey_GeneralGaussianSplatShBuffer {
        .id = vbase::hashLiteral("GeneralGaussianSplatShBuffer")};
    // set = 0, binding = 24
    constexpr FrameGraphResourceKey kResKey_VisibleInstanceBuffer {.id = vbase::hashLiteral("VisibleInstanceBuffer")};
    // set = 0, binding = 25
    constexpr FrameGraphResourceKey kResKey_VisibleInstanceCountBuffer {
        .id = vbase::hashLiteral("VisibleInstanceCountBuffer")};
    // set = 0, binding = 26
    constexpr FrameGraphResourceKey kResKey_MeshletCullDispatchArgsBuffer {
        .id = vbase::hashLiteral("MeshletCullDispatchArgsBuffer")};
    // set = 0, binding = 30
    constexpr FrameGraphResourceKey kResKey_DrawSetBuffer {.id = vbase::hashLiteral("DrawSetBuffer")};

    // -------- Internal resources --------
    constexpr FrameGraphResourceKey kResKey_FinalCompositionSource {.id = vbase::hashLiteral("FinalCompositionSource")};
    constexpr FrameGraphResourceKey kResKey_DepthTexture {.id = vbase::hashLiteral("DepthTexture")};
    constexpr FrameGraphResourceKey kResKey_HzbTexture {.id = vbase::hashLiteral("HZBTexture")};
} // namespace vultra
