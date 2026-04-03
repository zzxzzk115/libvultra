#include "vultra/core/rhi/backends/webgpu/webgpu_command_buffer_access.hpp"

#include "vultra/core/rhi/backends/webgpu/webgpu_command_buffer.hpp"
#include "vultra/core/rhi/command_buffer.hpp"

namespace vultra
{
    namespace rhi
    {
        bool WebGPUCommandBufferAccess::isWebGPUCommandBuffer(const CommandBuffer& cb)
        {
            return dynamic_cast<const WebGPUCommandBuffer*>(cb.m_Impl.get()) != nullptr;
        }

        WGPURenderPassEncoder WebGPUCommandBufferAccess::getCurrentRenderPassEncoder(const CommandBuffer& cb)
        {
            if (const auto* webgpuCb = dynamic_cast<const WebGPUCommandBuffer*>(cb.m_Impl.get()); webgpuCb)
            {
                return webgpuCb->getCurrentRenderPassEncoder();
            }
            return nullptr;
        }
    } // namespace rhi
} // namespace vultra
