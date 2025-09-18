#pragma once

#include "vultra/core/rhi/graphics_pipeline.hpp"

namespace vultra
{
    namespace gfx
    {
        [[nodiscard]] rhi::GraphicsPipeline
        createPostProcessPipelineFromSPV(rhi::RenderDevice&, const rhi::PixelFormat colorFormat, const rhi::SPIRV& spv);
    } // namespace gfx
} // namespace vultra