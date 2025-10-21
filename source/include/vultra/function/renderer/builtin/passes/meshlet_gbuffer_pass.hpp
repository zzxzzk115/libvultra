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
        class MeshletGBufferPass final : public rhi::RenderPass<MeshletGBufferPass>
        {
            friend class BasePass;

        public:
            explicit MeshletGBufferPass(rhi::RenderDevice&);

            void addPass(FrameGraph&,
                         FrameGraphBlackboard&,
                         const rhi::Extent2D&   resolution,
                         const RenderableGroup& renderableGroup,
                         bool                   enableNormalMapping,
                         uint32_t               debugMode);

        private:
            rhi::GraphicsPipeline createPipeline(const gfx::BaseGeometryPassInfo&, uint32_t) const;
        };
    } // namespace gfx
} // namespace vultra