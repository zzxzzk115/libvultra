#pragma once

#include "vultra/core/rhi/raytracing/acceleration_structure.hpp"

#include <glm/mat4x4.hpp>

namespace vultra
{
    namespace rhi
    {
        struct RayTracingInstance
        {
            AccelerationStructure*       blas {nullptr};
            glm::mat4                    transform {1.0f};
            uint32_t                     instanceID {0};
            uint32_t                     mask {0xFF};
            uint32_t                     sbtRecordOffset {0};
            vk::GeometryInstanceFlagsKHR flags {vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable};
        };
    } // namespace rhi
} // namespace vultra