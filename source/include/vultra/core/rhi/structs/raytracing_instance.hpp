#pragma once

#include "vultra/core/rhi/acceleration_structure.hpp"
#include "vultra/core/rhi/structs/raytracing_instance_flags.hpp"

#include <glm/mat4x4.hpp>

namespace vultra
{
    namespace rhi
    {
        struct RayTracingInstance
        {
            AccelerationStructure*  blas {nullptr};
            glm::mat4               transform {1.0f};
            uint32_t                instanceID {0};
            uint32_t                mask {0xFF};
            uint32_t                sbtRecordOffset {0};
            RayTracingInstanceFlags flags {RayTracingInstanceFlags::eTriangleFacingCullDisable};
        };
    } // namespace rhi
} // namespace vultra
