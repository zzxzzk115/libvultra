#pragma once

#include "vultra/core/rhi/backends/webgpu/webgpu_render_device.hpp"
#include "vultra/core/rhi/structs/extent2d.hpp"

namespace vultra
{
    namespace rhi
    {
        class CommandBuffer;

        class WebGPUCommandBufferAccess
        {
        public:
            [[nodiscard]] static bool isWebGPUCommandBuffer(const CommandBuffer&);
            [[nodiscard]] static WGPURenderPassEncoder getCurrentRenderPassEncoder(const CommandBuffer&);
            // Extent of the pass's render target ({0,0} when unknown). Callers that record viewports
            // themselves (e.g. the ImGui backend) must not exceed it or the encoder is invalidated.
            [[nodiscard]] static Extent2D getCurrentTargetExtent(const CommandBuffer&);
            static void closeActiveComputePassForProfilingBoundary(CommandBuffer&);
        };
    } // namespace rhi
} // namespace vultra
