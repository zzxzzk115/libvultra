#include "vultra/core/rhi/backends/webgpu/webgpu_pipeline.hpp"

#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
#include <webgpu/webgpu.h>
#endif

namespace vultra
{
    namespace rhi
    {
        void WebGPUPipeline::destroy(const std::uintptr_t pipelineHandle) noexcept
        {
#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
            if (pipelineHandle != 0)
            {
                wgpuRenderPipelineRelease(reinterpret_cast<WGPURenderPipeline>(pipelineHandle));
            }
#else
            (void)pipelineHandle;
#endif
        }
    } // namespace rhi
} // namespace vultra
