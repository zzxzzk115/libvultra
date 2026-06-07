#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace vultra::resource
{
    enum class GpuMaterialModel : uint32_t
    {
        eInvalid = 0,
        ePBRMetallicRoughness,
        ePBRSpecularGlossiness,
        eUnlit,
        ePhong,
        eMaterialGraph,
        eShaderMaterial,
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
