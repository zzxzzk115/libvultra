#include "vultra/core/rhi/backends/webgpu/webgpu_buffer.hpp"
#include "vultra/core/rhi/interfaces/irender_device.hpp"

#include <cassert>

#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
#include <webgpu/webgpu.h>
#endif

namespace vultra
{
    namespace rhi
    {
        WebGPUBuffer::WebGPUBuffer(IRenderDevice*         renderDevice,
                                   const uint64_t         size,
                                   const std::uintptr_t   handle,
                                   const std::uintptr_t   queueHandle) :
            m_RenderDevice(renderDevice), m_Data(static_cast<size_t>(size)), m_Size(size), m_Handle(handle), m_QueueHandle(queueHandle),
            m_Valid(handle != 0)
        {
            if (m_RenderDevice)
            {
                m_RenderDevice->onMemoryAllocated(RenderMemoryKind::eCpuCache, m_Size);
                m_RenderDevice->onMemoryAllocated(RenderMemoryKind::eGpuHostVisible, m_Size);
            }
        }

        WebGPUBuffer::~WebGPUBuffer()
        {
#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
            if (m_Handle != 0)
            {
                wgpuBufferRelease(reinterpret_cast<WGPUBuffer>(m_Handle));
                m_Handle = 0;
            }
#endif
            if (m_RenderDevice)
            {
                m_RenderDevice->onMemoryFreed(RenderMemoryKind::eCpuCache, m_Size);
                m_RenderDevice->onMemoryFreed(RenderMemoryKind::eGpuHostVisible, m_Size);
                m_RenderDevice = nullptr;
            }
            m_Valid = false;
        }

        void* WebGPUBuffer::map()
        {
            assert(m_Valid);
            m_Mapped = true;
            return m_Data.data();
        }

        void WebGPUBuffer::unmap()
        {
            assert(m_Valid);
#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
            if (m_Handle != 0 && m_QueueHandle != 0 && !m_Data.empty())
            {
                wgpuQueueWriteBuffer(reinterpret_cast<WGPUQueue>(m_QueueHandle),
                                     reinterpret_cast<WGPUBuffer>(m_Handle),
                                     0,
                                     m_Data.data(),
                                     m_Data.size());
            }
#endif
            m_Mapped = false;
        }

        void WebGPUBuffer::flush(const uint64_t, const uint64_t)
        {
            assert(m_Valid);
            // Host memory backing in current minimal WebGPU path: no explicit flush needed.
        }
    } // namespace rhi
} // namespace vultra
