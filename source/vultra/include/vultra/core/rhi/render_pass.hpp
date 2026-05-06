#pragma once

#include "vultra/core/rhi/base_pass.hpp"
#include "vultra/core/rhi/graphics_pipeline.hpp"

namespace vultra
{
    namespace rhi
    {
        template<class TargetPass>
        using RenderPass = BasePass<TargetPass, GraphicsPipeline>;
    } // namespace rhi
} // namespace vultra