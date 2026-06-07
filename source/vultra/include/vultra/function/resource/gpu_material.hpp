#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace vultra::resource
{
    // Values are the GBuffer / deferred-lighting model codes: thin_gbuffer writes the
    // raw enum value into GBufferMaterial.a and deferred_lighting branches on it
    // (VULTRA_MAT_PBRMR=1, PBRSG=2, Unlit=3, Phong=4, ToonLike=6). Keep these in sync
    // with the VULTRA_MAT_* shader constants and material_graph gbufferModelCode().
    // (5 is reserved; it was the removed eMaterialGraph parametric path. Material
    // graphs now pack a real per-model block, exactly like a hand-authored material.)
    // eShaderMaterial's value is NOT a GBuffer code (shader materials write the GBuffer
    // from their own eval.shadingModel), so it sits past the builtin lighting codes.
    enum class GpuMaterialModel : uint32_t
    {
        eInvalid               = 0,
        ePBRMetallicRoughness  = 1,
        ePBRSpecularGlossiness = 2,
        eUnlit                 = 3,
        ePhong                 = 4,
        eToon                  = 6,
        eShaderMaterial        = 7,
    };

    struct GpuMaterial
    {
        // Material model routing (fast-path). Extension/custom parameters are handled by MaterialBlock.
        GpuMaterialModel model {GpuMaterialModel::eInvalid};

        // Offset (bytes) into the global GPU material parameter buffer.
        uint32_t blockOffsetBytes {0};

        // Optional index into a GPU-side material table.
        uint32_t tableIndex {0};

        // Reserved for future use
        uint32_t padding {0};
    };

    static_assert(sizeof(GpuMaterial) % 16 == 0, "GpuMaterial must be 16-byte aligned for std430 buffer layout");

    struct ShaderMaterialRuntimeInfo
    {
        std::string shaderLibraryUri;
        std::string fragmentShaderId;
        uint64_t    fragmentVariantHash {0};
        // Entity-id-writing variant of the fragment, used when the GBuffer pass
        // also writes the entity-id attachment (selection/picking). 0 when there
        // is no such variant (e.g. plain shader materials).
        uint64_t    fragmentVariantHashEntityId {0};
        uint32_t    materialParamSize {0};
    };

    using ShaderMaterialRuntimeInfoMap = std::unordered_map<uint32_t, ShaderMaterialRuntimeInfo>;
} // namespace vultra::resource
