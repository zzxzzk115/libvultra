#pragma once

#include "vultra/core/rhi/backends/webgpu/webgpu_render_device.hpp"

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
        };
    } // namespace rhi
} // namespace vultra
