#pragma once

#include <cstdint>

namespace vultra::resource
{
    enum class GpuMaterialModel : uint8_t
    {
        eInvalid = 0,
        ePBRMetallicRoughness,
        ePBRSpecularGlossiness,
        eUnlit,
        ePhong,
    };

    struct GpuMaterial
    {
        // In the future this will be provided by vshadersystem (shader library key hash / permutation hash).
        uint64_t shaderIdHash {0};

        // Material model routing (fast-path). Extension/custom parameters are handled by MaterialBlock.
        GpuMaterialModel model {GpuMaterialModel::eInvalid};

        // Offset (bytes) into the global GPU material parameter buffer.
        uint32_t blockOffsetBytes {0};

        // Optional index into a GPU-side material table.
        uint32_t tableIndex {0};
    };
} // namespace vultra::resource
