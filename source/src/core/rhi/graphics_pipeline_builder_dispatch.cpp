#include "vultra/core/rhi/graphics_pipeline.hpp"

#include "vultra/core/rhi/render_device.hpp"

namespace vultra
{
    namespace rhi
    {
        GraphicsPipeline GraphicsPipeline::Builder::build(RenderDevice& rd)
        {
            if (rd.getBackendApi() == RenderBackendApi::eWebGPU)
            {
                if (auto webgpuPipeline = buildWebGPU(rd); webgpuPipeline.has_value())
                {
                    return std::move(*webgpuPipeline);
                }
                return {};
            }

            return buildVulkan(rd);
        }
    } // namespace rhi
} // namespace vultra
