#include "vultra/core/rhi/backends/webgpu/webgpu_compute_pipeline_destroy.hpp"

#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
#include <webgpu/webgpu.h>
#endif

namespace vultra
{
    namespace rhi
    {
        void WebGPUComputePipelineDestroy::destroy(const std::uintptr_t handle) noexcept
        {
#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
            if (handle != 0)
            {
                wgpuComputePipelineRelease(reinterpret_cast<WGPUComputePipeline>(handle));
            }
#else
            (void)handle;
#endif
        }
    } // namespace rhi
} // namespace vultra
