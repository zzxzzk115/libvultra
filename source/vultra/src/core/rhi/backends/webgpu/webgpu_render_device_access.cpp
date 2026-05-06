#include "vultra/core/rhi/backends/webgpu/webgpu_render_device_access.hpp"

#include "vultra/core/base/base.hpp"
#include "vultra/core/rhi/backends/webgpu/webgpu_render_device.hpp"
#include "vultra/core/rhi/interfaces/render_device_access.hpp"
#include "vultra/core/rhi/render_device.hpp"

#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
#include <webgpu/webgpu.h>
#endif

namespace vultra
{
    namespace rhi
    {
        bool WebGPURenderDeviceAccess::isWebGPUBackend(const RenderDevice& rd)
        {
            return dynamic_cast<const WebGPURenderDevice*>(RenderDeviceAccess::get(rd)) != nullptr;
        }

        std::uintptr_t WebGPURenderDeviceAccess::getInstanceHandle(const RenderDevice& rd)
        {
            if (const auto* backend = dynamic_cast<const WebGPURenderDevice*>(RenderDeviceAccess::get(rd)); backend)
            {
                return reinterpret_cast<std::uintptr_t>(backend->m_Instance);
            }
            return 0;
        }

        std::uintptr_t WebGPURenderDeviceAccess::getAdapterHandle(const RenderDevice& rd)
        {
            if (const auto* backend = dynamic_cast<const WebGPURenderDevice*>(RenderDeviceAccess::get(rd)); backend)
            {
                return reinterpret_cast<std::uintptr_t>(backend->m_Adapter);
            }
            return 0;
        }

        std::uintptr_t WebGPURenderDeviceAccess::getDeviceHandle(const RenderDevice& rd)
        {
            if (const auto* backend = dynamic_cast<const WebGPURenderDevice*>(RenderDeviceAccess::get(rd)); backend)
            {
                return reinterpret_cast<std::uintptr_t>(backend->m_Device);
            }
            return 0;
        }

        std::uintptr_t WebGPURenderDeviceAccess::getQueueHandle(const RenderDevice& rd)
        {
            if (const auto* backend = dynamic_cast<const WebGPURenderDevice*>(RenderDeviceAccess::get(rd)); backend)
            {
                return reinterpret_cast<std::uintptr_t>(backend->m_Queue);
            }
            return 0;
        }

        bool WebGPURenderDeviceAccess::submitCommandBuffer(const RenderDevice&  rd,
                                                           const std::uintptr_t commandBufferHandle)
        {
#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
            if (commandBufferHandle == 0)
            {
                return false;
            }
            if (const auto* backend = dynamic_cast<const WebGPURenderDevice*>(RenderDeviceAccess::get(rd)); backend)
            {
                if (backend->m_Queue != nullptr)
                {
                    auto* const commandBuffer = reinterpret_cast<WGPUCommandBuffer>(commandBufferHandle);
                    wgpuQueueSubmit(backend->m_Queue, 1, &commandBuffer);
                    return true;
                }
            }
#else
            (void)rd;
            (void)commandBufferHandle;
#endif
            return false;
        }

        void WebGPURenderDeviceAccess::processEvents(const RenderDevice& rd)
        {
#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
            if (const auto* backend = dynamic_cast<const WebGPURenderDevice*>(RenderDeviceAccess::get(rd)); backend)
            {
                if (backend->m_Instance != nullptr)
                {
                    wgpuInstanceProcessEvents(backend->m_Instance);
                }
            }
#else
            (void)rd;
#endif
        }
    } // namespace rhi
} // namespace vultra
