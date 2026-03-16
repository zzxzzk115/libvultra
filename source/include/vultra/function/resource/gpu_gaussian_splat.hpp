#pragma once

#include "vultra/core/base/base.hpp"
#include "vultra/core/rhi/storage_buffer.hpp"

#include <glm/glm.hpp>

#include <cstdint>

namespace vultra::resource
{
    // GPU representation for one imported gaussian splat cloud.
    // Buffers are packed to match the gaussian_splatting example shaders:
    // - centers: vec4(xyz, 1)
    // - covariances: uvec4 packed half pairs (m11,m12) (m13,m22) (m23,m33)
    // - colors: uvec2 packed half pairs (r,g) (b,a)
    // - sh: uvec2 packed half pairs per coeff, fixed 15 coeffs / point
    struct GpuGaussianSplat
    {
        static constexpr uint32_t s_PackedShRestCoeffs = 15;

        uint32_t pointCount {0};
        int32_t  shDegree {0};
        uint32_t shRestCoeffCount {s_PackedShRestCoeffs};
        uint32_t pad0 {0};

        glm::vec3 center {0.0f};
        float     radius {0.0f};

        Ref<rhi::StorageBuffer> centersBuffer {nullptr};
        Ref<rhi::StorageBuffer> covarianceBuffer {nullptr};
        Ref<rhi::StorageBuffer> colorBuffer {nullptr};
        Ref<rhi::StorageBuffer> shBuffer {nullptr};
    };
} // namespace vultra::resource
