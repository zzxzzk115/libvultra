#pragma once

#include "vultra/core/rhi/extent2d.hpp"
#include "vultra/core/rhi/render_pass.hpp"
#include "vultra/function/renderer/base_geometry_pass_info.hpp"
#include "vultra/function/renderer/renderable.hpp"

#include <fg/Fwd.hpp>

namespace vultra
{
    namespace gfx
    {
        class DepthPrePass final : public rhi::RenderPass<DepthPrePass>
        {
            friend class BasePass;

        public:
            explicit DepthPrePass(rhi::RenderDevice&);

            void addPass(FrameGraph&,
                         FrameGraphBlackboard&,
                         const rhi::Extent2D&        resolution,
                         const RenderPrimitiveGroup& renderPrimitiveGroup);

        private:
            rhi::GraphicsPipeline createPipeline(const gfx::BaseGeometryPassInfo&) const;
        };
    } // namespace gfx
} // namespace vultra