#pragma once

#include "vultra/core/rhi/extent2d.hpp"
#include "vultra/core/rhi/raytracing/raytracing_pass.hpp"
#include "vultra/function/renderer/renderable.hpp"

#include <fg/Fwd.hpp>

namespace vultra
{
    namespace gfx
    {
        class SimpleRaytracingPass final : public rhi::RayTracingPass<SimpleRaytracingPass>
        {
            friend class BasePass;

        public:
            explicit SimpleRaytracingPass(rhi::RenderDevice&);

            [[nodiscard]] FrameGraphResource addPass(FrameGraph&,
                                                     FrameGraphBlackboard&,
                                                     const rhi::Extent2D&        resolution,
                                                     const RenderPrimitiveGroup& renderPrimitiveGroup,
                                                     uint32_t                    maxRecursionDepth,
                                                     const glm::vec4&            missColor);

        private:
            rhi::RayTracingPipeline createPipeline(uint32_t maxRecursionDepth) const;

            rhi::ShaderBindingTable m_SBT;
        };
    } // namespace gfx
} // namespace vultra