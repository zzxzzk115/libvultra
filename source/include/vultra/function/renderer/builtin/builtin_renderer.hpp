#pragma once

#include "vultra/function/framegraph/render_context.hpp"
#include "vultra/function/framegraph/transient_resources.hpp"
#include "vultra/function/renderer/base_renderer.hpp"
#include "vultra/function/renderer/builtin/upload_resources.hpp"

namespace vultra
{
    namespace gfx
    {
        class GBufferPass;
        class FinalPass;

        class BuiltinRenderer : public BaseRenderer
        {
        public:
            BuiltinRenderer(rhi::RenderDevice& rd);
            ~BuiltinRenderer() override;

            virtual void render(rhi::CommandBuffer& cb, rhi::Texture* renderTarget, const fsec dt) override final;

            CameraInfo& getCameraInfo() { return m_CameraInfo; }
            LightInfo&  getLightInfo() { return m_LightInfo; }

            void setScene(LogicScene* scene) override;

        private:
            void setupSamplers();

        private:
            framegraph::Samplers           m_Samplers;
            framegraph::TransientResources m_TransientResources;

            CameraInfo m_CameraInfo {};
            FrameInfo  m_FrameInfo {};
            LightInfo  m_LightInfo {};

            GBufferPass* m_GBufferPass {nullptr};
            FinalPass*   m_FinalPass {nullptr};
        };
    } // namespace gfx
} // namespace vultra