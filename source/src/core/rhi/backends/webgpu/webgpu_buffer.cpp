#include "vultra/core/rhi/backends/webgpu/webgpu_buffer.hpp"

#include <cassert>

#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
#include <webgpu/webgpu.h>
#endif

namespace vultra
{
    namespace rhi
    {
        WebGPUBuffer::WebGPUBuffer(const uint64_t size, const std::uintptr_t handle, const std::uintptr_t queueHandle) :
            m_Data(static_cast<size_t>(size)), m_Size(size), m_Handle(handle), m_QueueHandle(queueHandle),
            m_Valid(handle != 0)
        {}

        WebGPUBuffer::~WebGPUBuffer()
        {
#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
            if (m_Handle != 0)
            {
                wgpuBufferRelease(reinterpret_cast<WGPUBuffer>(m_Handle));
                m_Handle = 0;
            }
#endif
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
