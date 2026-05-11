#pragma once

#include "vultra/core/rhi/structs/device_address.hpp"

#include <cstdint>

#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>

namespace vultra::resource
{
    enum class GpuDrawFlags : uint32_t
    {
        eNone = 0,
        eMeshlet = 1u << 0,
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

        rhi::DeviceAddress vertexAddress {};
        // Scene instance payload index (or transform index for legacy CPU-driven path).
        uint32_t instanceIndex {0};
        uint32_t padding0 {0};

        glm::mat4 model {1.0f};
    };

    static_assert(sizeof(GpuDrawRecord) % 16 == 0, "GpuDrawRecord must be 16-byte aligned");

    // Unified eGeneral 3DGS draw contract. This is intentionally backend-agnostic
    // and sized in 16-byte chunks so Vulkan/WebGPU can share one source layout.
    struct GpuGeneralGaussianSplatDrawRecord
    {
        uint32_t splatIndex {0};
        uint32_t pointOffset {0};
        uint32_t pointCount {0};
        uint32_t shDegree {0};
        glm::vec4 params0 {0.3f, 1.0f, 1.0f, 0.0f}; // x=kernelSize, y=cutoffScale, z=opacityScale, w=reserved
        glm::mat4 model {1.0f};
    };
    static_assert(sizeof(GpuGeneralGaussianSplatDrawRecord) == 96,
                  "GpuGeneralGaussianSplatDrawRecord must remain tightly packed");

    // Visionary-style packed source record. The exact bit packing will tighten
    // later, but the contract is already a compact u32 payload instead of the
    // legacy split center/cov/color/meta indirections.
    struct GpuGeneralGaussianSplatPackedSource
    {
        glm::uvec4 posOpacity {0u};
        glm::uvec4 covariance0 {0u};
        glm::uvec4 colorSh0 {0u};
        glm::uvec4 aux0 {0u}; // x=sourcePointIndex, y=reserved, z=shCoeffOffset, w=flags
    };
    static_assert(sizeof(GpuGeneralGaussianSplatPackedSource) == 64,
                  "GpuGeneralGaussianSplatPackedSource must remain 64 bytes");

    // Per-scene Gaussian source indirection. Imported CLOD assets are physically
    // sorted by importance, so runtime LOD only changes the active prefix length.
    // The table remains separate from packed data so baseline and Ordered CLOD can
    // share one preprocess shader path.
    struct GpuGeneralGaussianSplatSelectedSource
    {
        // Index into GpuGeneralGaussianSplatPackedSource.
        uint32_t sourceIndex {0};
        // Draw record that owns the source point and provides the model matrix.
        uint32_t drawIndex {0};
        // Reserved opacity multiplier. Zero is treated as 1.0 in shader code.
        uint32_t packedWeight {0};
        // Per-entry metadata, currently unused outside the shader invalid bit.
        uint32_t flags {0};
    };
    static_assert(sizeof(GpuGeneralGaussianSplatSelectedSource) == 16,
                  "GpuGeneralGaussianSplatSelectedSource must remain 16 bytes");

    // Screen-space payload mirrors Visionary's compact Splat2D contract:
    // packed axes, packed NDC center, high-precision depth, packed RGBA.
    // XR multiview stores one payload per eye so the render pass can select
    // the correct screen-space data via gl_ViewIndex.
    struct GpuGeneralGaussianSplatVisibleSplat
    {
        glm::uvec4 packedEye0_0 {0u}; // x=basis0.xy, y=basis1.xy, z=centerNdc.xy, w=floatBits(depth)
        glm::uvec4 packedEye0_1 {0u}; // x=color.rg, y=color.ba, z=packedSourceIndex, w=drawIndex
        glm::uvec4 packedEye1_0 {0u}; // x=basis0.xy, y=basis1.xy, z=centerNdc.xy, w=floatBits(depth)
        glm::uvec4 packedEye1_1 {0u}; // x=color.rg, y=color.ba, z=packedSourceIndex, w=drawIndex
    };
    static_assert(sizeof(GpuGeneralGaussianSplatVisibleSplat) == 64,
                  "GpuGeneralGaussianSplatVisibleSplat must remain tightly packed");
} // namespace vultra::resource
