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
        class GBufferPass final : public rhi::RenderPass<GBufferPass>
        {
            friend class BasePass;

        public:
            explicit GBufferPass(rhi::RenderDevice&);

            void addPass(FrameGraph&,
                         FrameGraphBlackboard&,
                         const rhi::Extent2D&        resolution,
                         const RenderPrimitiveGroup& renderPrimitiveGroup,
                         bool                        enableAreaLight);

        private:
            rhi::GraphicsPipeline
            createPipeline(const gfx::BaseGeometryPassInfo&, bool doubleSided, bool alphaMasking) const;

            rhi::GraphicsPipeline m_AreaLightDebugPipeline;
            bool                  m_AreaLightDebugCreated {false};

            rhi::GraphicsPipeline m_DecalPipeline;
            bool                  m_DecalPipelineCreated {false};
        };
    } // namespace gfx
} // namespace vultra