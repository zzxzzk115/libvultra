#pragma once

#include <cstdint>

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
} // namespace vultra::resource
