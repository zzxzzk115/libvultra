#pragma once

#include "vultra/core/base/base.hpp"
#include "vultra/core/rhi/storage_buffer.hpp"

#include <glm/glm.hpp>

#include <cstdint>

namespace vultra::resource
{
    struct GpuGaussianSplatMeta
    {
        uint32_t pointOffset {0};
        uint32_t pointCount {0};
        uint32_t shDegree {0};
        uint32_t shRestCoeffCount {0};
    };
    static_assert(sizeof(GpuGaussianSplatMeta) == 16, "GpuGaussianSplatMeta must remain tightly packed");

    // GPU representation for one imported gaussian splat cloud.
    // Point data is appended into a global storage pool:
    // - centers: vec4(xyz, 1)
    // - covariances: uvec4 packed half pairs (m11,m12) (m13,m22) (m23,m33)
    // - colors: uvec2 packed half pairs (r,g) (b,a)
    // - sh: uvec2 packed half pairs per coeff, fixed 15 coeffs / point
    struct GpuGaussianSplat
    {
        static constexpr uint32_t s_PackedShRestCoeffs = 15;

        uint32_t pointCount {0};
        uint32_t pointOffset {0};
        int32_t  shDegree {0};
        uint32_t shRestCoeffCount {s_PackedShRestCoeffs};

        glm::vec3 center {0.0f};
        float     radius {0.0f};
    };
} // namespace vultra::resource
