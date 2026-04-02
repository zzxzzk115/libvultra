#pragma once

#include <cstdint>

namespace vultra
{
    namespace rhi
    {
        enum class RayTracingInstanceFlags : uint32_t
        {
            eNone                       = 0,
            eTriangleFacingCullDisable  = 0x00000001,
            eTriangleFlipFacing         = 0x00000002,
            eForceOpaque                = 0x00000004,
            eForceNoOpaque              = 0x00000008,
            eForceOpacityMicromap2State = 0x00000010,
            eDisableOpacityMicromaps    = 0x00000020,
        };
    } // namespace rhi
} // namespace vultra
