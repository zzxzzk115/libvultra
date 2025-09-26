#pragma once

#include <cstdint>

namespace vultra
{
    namespace rhi
    {
        struct RaytracingPipelineProperties
        {
            uint32_t shaderGroupHandleSize;
            uint32_t maxRayRecursionDepth;
            uint32_t maxShaderGroupStride;
            uint32_t shaderGroupBaseAlignment;
            uint32_t shaderGroupHandleCaptureReplaySize;
            uint32_t maxRayDispatchInvocationCount;
            uint32_t shaderGroupHandleAlignment;
            uint32_t maxRayHitAttributeSize;
        };
    } // namespace rhi
} // namespace vultra