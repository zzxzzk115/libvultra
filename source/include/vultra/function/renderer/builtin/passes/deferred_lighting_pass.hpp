#pragma once

#include "vultra/core/rhi/render_pass.hpp"
#include "vultra/core/rhi/texture.hpp"

#include <fg/Fwd.hpp>
#include <glm/glm.hpp>

namespace vultra
{
    namespace gfx
    {
        class DeferredLightingPass final : public rhi::RenderPass<DeferredLightingPass>
        {
            friend class BasePass;

        public:
            explicit DeferredLightingPass(rhi::RenderDevice&);

            void addPass(FrameGraph&,
                         FrameGraphBlackboard&,
                         bool      enableAreaLight,
                         glm::vec4 clearColor = {0.0f, 0.0f, 0.0f, 1.0f});

        private:
            rhi::GraphicsPipeline createPipeline() const;

            // LTC lookup textures (builtin ltc_1.dds, ltc_2.dds)
            Ref<vultra::rhi::Texture> m_LTCMat; // inverse matrix LUT
            Ref<vultra::rhi::Texture> m_LTCMag; // magnitude/fresnel LUT
        };
    } // namespace gfx
} // namespace vultra