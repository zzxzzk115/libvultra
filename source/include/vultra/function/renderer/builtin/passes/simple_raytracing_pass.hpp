#pragma once

#include "vultra/core/rhi/extent2d.hpp"
#include "vultra/core/rhi/raytracing/raytracing_pass.hpp"
#include "vultra/function/renderer/builtin/tonemapping_method.hpp"
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
                                                     const std::span<Renderable> renderables,
                                                     uint32_t                    maxRecursionDepth,
                                                     const glm::vec4&            missColor,
                                                     uint32_t                    mode,
                                                     bool                        enableNormalMapping,
                                                     bool                        enableAreaLights,
                                                     bool                        enableIBL,
                                                     float                       exposure,
                                                     ToneMappingMethod           toneMappingMethod);

        private:
            rhi::RayTracingPipeline createPipeline(uint32_t maxRecursionDepth) const;

            rhi::ShaderBindingTable m_SBT;

            // LTC lookup textures (builtin ltc_1.dds, ltc_2.dds)
            Ref<vultra::rhi::Texture> m_LTCMat; // inverse matrix LUT
            Ref<vultra::rhi::Texture> m_LTCMag; // magnitude/fresnel LUT
        };
    } // namespace gfx
} // namespace vultra