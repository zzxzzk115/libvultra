#pragma once

#include <cstdint>

#include <glm/mat4x4.hpp>

namespace vultra::resource
{
    enum class GpuDrawFlags : uint32_t
    {
        eNone = 0,
        eMeshlet = 1u << 0,
        eGaussianSplat = 1u << 1,
    };

    inline constexpr uint32_t gpuDrawFlagsToMask(GpuDrawFlags f)
    {
        return static_cast<uint32_t>(f);
    }

    inline constexpr bool gpuDrawHasFlag(uint32_t flags, GpuDrawFlags f)
    {
        return (flags & gpuDrawFlagsToMask(f)) != 0u;
    }

    // Primitive-agnostic per-draw record.
    // One indirect draw == one primitive instance (meshlet/splat/...)
    //
    // Notes:
    // - instanceIndex is the canonical path for the future GPU-driven compute
    //   build-indirect pipeline.
    // - model is intentionally retained for the existing CPU-driven path and
    //   for debugging/inspection while both pipelines coexist.
    struct GpuDrawRecord
    {
        // Primitive payload index (meshlet index, splat index, ...)
        uint32_t primitiveIndex {0};
        uint32_t materialIndex {0};
        uint32_t vertexStrideBytes {0};
        // Bitmask from GpuDrawFlags.
        uint32_t flags {gpuDrawFlagsToMask(GpuDrawFlags::eNone)};

        uint64_t vertexAddress {0};
        // Scene instance payload index (or transform index for legacy CPU-driven path).
        uint32_t instanceIndex {0};
        uint32_t padding0 {0};

        glm::mat4 model {1.0f};
    };

    static_assert(sizeof(GpuDrawRecord) % 16 == 0, "GpuDrawRecord must be 16-byte aligned");
} // namespace vultra::resource
