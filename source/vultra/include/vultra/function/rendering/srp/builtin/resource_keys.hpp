#pragma once

#include "vultra/function/framegraph/framegraph_data_registry.hpp"
#include "vultra/function/resource/gpu_scene_view.hpp"

#include <array>

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
    // set = 0, binding = 27
    constexpr FrameGraphResourceKey kResKey_GeneralGaussianSplatSelectedSourceBuffer {
        .id = vbase::hashLiteral("GeneralGaussianSplatSelectedSourceBuffer")};
    // set = 0, bindings = 31, 32, 33
    inline constexpr std::array<FrameGraphResourceKey, resource::kGeneralGaussianSplatFoveatedLayerCount>
        kResKey_GeneralGaussianSplatFoveatedVisibleSplatBuffers {
        FrameGraphResourceKey {.id = vbase::hashLiteral("GeneralGaussianSplatFoveatedFoveaVisibleSplatBuffer")},
        FrameGraphResourceKey {.id = vbase::hashLiteral("GeneralGaussianSplatFoveatedMidVisibleSplatBuffer")},
        FrameGraphResourceKey {.id = vbase::hashLiteral("GeneralGaussianSplatFoveatedOuterVisibleSplatBuffer")}};
    // set = 0, bindings = 34, 35, 36
    inline constexpr std::array<FrameGraphResourceKey, resource::kGeneralGaussianSplatFoveatedLayerCount>
        kResKey_GeneralGaussianSplatFoveatedSortKeyBuffers {
        FrameGraphResourceKey {.id = vbase::hashLiteral("GeneralGaussianSplatFoveatedFoveaSortKeyBuffer")},
        FrameGraphResourceKey {.id = vbase::hashLiteral("GeneralGaussianSplatFoveatedMidSortKeyBuffer")},
        FrameGraphResourceKey {.id = vbase::hashLiteral("GeneralGaussianSplatFoveatedOuterSortKeyBuffer")}};
    // set = 0, bindings = 37, 38, 39
    inline constexpr std::array<FrameGraphResourceKey, resource::kGeneralGaussianSplatFoveatedLayerCount>
        kResKey_GeneralGaussianSplatFoveatedSortIndexBuffers {
        FrameGraphResourceKey {.id = vbase::hashLiteral("GeneralGaussianSplatFoveatedFoveaSortIndexBuffer")},
        FrameGraphResourceKey {.id = vbase::hashLiteral("GeneralGaussianSplatFoveatedMidSortIndexBuffer")},
        FrameGraphResourceKey {.id = vbase::hashLiteral("GeneralGaussianSplatFoveatedOuterSortIndexBuffer")}};
    // set = 0, bindings = 40, 41, 42
    inline constexpr std::array<FrameGraphResourceKey, resource::kGeneralGaussianSplatFoveatedLayerCount>
        kResKey_GeneralGaussianSplatFoveatedVisibleCountBuffers {
        FrameGraphResourceKey {.id = vbase::hashLiteral("GeneralGaussianSplatFoveatedFoveaVisibleCountBuffer")},
        FrameGraphResourceKey {.id = vbase::hashLiteral("GeneralGaussianSplatFoveatedMidVisibleCountBuffer")},
        FrameGraphResourceKey {.id = vbase::hashLiteral("GeneralGaussianSplatFoveatedOuterVisibleCountBuffer")}};
    // set = 0, bindings = 43, 44, 45
    inline constexpr std::array<FrameGraphResourceKey, resource::kGeneralGaussianSplatFoveatedLayerCount>
        kResKey_GeneralGaussianSplatFoveatedIndirectBuffers {
        FrameGraphResourceKey {.id = vbase::hashLiteral("GeneralGaussianSplatFoveatedFoveaIndirectBuffer")},
        FrameGraphResourceKey {.id = vbase::hashLiteral("GeneralGaussianSplatFoveatedMidIndirectBuffer")},
        FrameGraphResourceKey {.id = vbase::hashLiteral("GeneralGaussianSplatFoveatedOuterIndirectBuffer")}};
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
    // set = 0, binding = 46
    constexpr FrameGraphResourceKey kResKey_SkinMatrixBuffer {.id = vbase::hashLiteral("SkinMatrixBuffer")};

    // -------- Internal resources --------
    constexpr FrameGraphResourceKey kResKey_FinalCompositionSource {.id = vbase::hashLiteral("FinalCompositionSource")};
    constexpr FrameGraphResourceKey kResKey_DepthTexture {.id = vbase::hashLiteral("DepthTexture")};
    constexpr FrameGraphResourceKey kResKey_HzbTexture {.id = vbase::hashLiteral("HZBTexture")};
    constexpr FrameGraphResourceKey kResKey_VisibilityBuffer {.id = vbase::hashLiteral("VisibilityBuffer")};
    constexpr FrameGraphResourceKey kResKey_GBufferColor {.id = vbase::hashLiteral("GBufferColor")};
    constexpr FrameGraphResourceKey kResKey_ThinGBufferColor {.id = vbase::hashLiteral("ThinGBufferColor")};
    constexpr FrameGraphResourceKey kResKey_GBufferNormal {.id = vbase::hashLiteral("GBufferNormal")};
    constexpr FrameGraphResourceKey kResKey_GBufferMaterial {
        .id = vbase::hashLiteral("GBufferMaterial")};
    constexpr FrameGraphResourceKey kResKey_GBufferEmissive {.id = vbase::hashLiteral("GBufferEmissive")};
    constexpr FrameGraphResourceKey kResKey_GBufferEntityId {.id = vbase::hashLiteral("GBufferEntityId")};
    constexpr FrameGraphResourceKey kResKey_SelectionOutlineOutput {.id = vbase::hashLiteral("SelectionOutlineOutput")};
    constexpr FrameGraphResourceKey kResKey_ShadowMap {.id = vbase::hashLiteral("ShadowMap")};
    constexpr FrameGraphResourceKey kResKey_ShadowData {.id = vbase::hashLiteral("ShadowData")};
    constexpr FrameGraphResourceKey kResKey_SsaoTexture {.id = vbase::hashLiteral("SSAOTexture")};
    constexpr FrameGraphResourceKey kResKey_SsrTexture {.id = vbase::hashLiteral("SSRTexture")};
    constexpr FrameGraphResourceKey kResKey_StereoColor {.id = vbase::hashLiteral("StereoColor")};
    constexpr FrameGraphResourceKey kResKey_StereoDepth {.id = vbase::hashLiteral("StereoDepth")};
    constexpr FrameGraphResourceKey kResKey_PreviousStereoColor {.id = vbase::hashLiteral("PreviousStereoColor")};
    constexpr FrameGraphResourceKey kResKey_PreviousStereoDepth {.id = vbase::hashLiteral("PreviousStereoDepth")};
    constexpr FrameGraphResourceKey kResKey_PreviousStereoPose {.id = vbase::hashLiteral("PreviousStereoPose")};
    constexpr FrameGraphResourceKey kResKey_StereoReprojectionMetadata {
        .id = vbase::hashLiteral("StereoReprojectionMetadata")};
} // namespace vultra
