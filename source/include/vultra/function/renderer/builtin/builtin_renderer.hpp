#pragma once

#include "vultra/function/framegraph/render_context.hpp"
#include "vultra/function/framegraph/transient_resources.hpp"
#include "vultra/function/renderer/base_renderer.hpp"
#include "vultra/function/renderer/builtin/pass_output_mode.hpp"
#include "vultra/function/renderer/builtin/tonemapping_method.hpp"
#include "vultra/function/renderer/builtin/tool/cubemap_converter.hpp"
#include "vultra/function/renderer/builtin/tool/ibl_data_generator.hpp"
#include "vultra/function/renderer/builtin/ui_structs.hpp"
#include "vultra/function/renderer/builtin/upload_resources.hpp"

namespace vultra
{
    namespace gfx
    {
        class GBufferPass;
        class DeferredLightingPass;
        class SkyboxPass;
        class ToneMappingPass;
        class GammaCorrectionPass;
        class FXAAPass;
        class FinalPass;
        class BlitPass;

        class UIPass;

        class SimpleRaytracingPass;

        enum class RendererType
        {
            eRasterization = 0,
            eRayTracing
        };

        struct BuiltinRenderSettings
        {
            // Rasterization or Ray Tracing
            RendererType rendererType {RendererType::eRasterization};

            // Rasterization settings
            PassOutputMode    outputMode {PassOutputMode::SceneColor_AntiAliased};
            bool              enableAreaLights {true};
            bool              enableNormalMapping {true};
            bool              enableIBL {true};
            float             exposure {1.0f};
            ToneMappingMethod toneMappingMethod {ToneMappingMethod::KhronosPBRNeutral};

            // Ray Tracing settings
            uint32_t maxRayRecursionDepth {2};
        };

        class BuiltinRenderer : public BaseRenderer
        {
        public:
            BuiltinRenderer(rhi::RenderDevice& rd, rhi::Swapchain::Format swapChainFormat);
            ~BuiltinRenderer() override;

            virtual void onImGui() override;

            virtual void render(rhi::CommandBuffer& cb, rhi::Texture* renderTarget, const fsec dt) override final;

            void renderXR(rhi::CommandBuffer& cb,
                          rhi::Texture*       leftEyeRenderTarget,
                          rhi::Texture*       rightEyeRenderTarget,
                          const fsec          dt);

            void beginFrame(rhi::CommandBuffer& cb) override;
            void endFrame() override;

            void drawCircleFilled(rhi::Texture*    target,
                                  const glm::vec2& position,
                                  float            radius,
                                  const glm::vec4& fillColor,
                                  const glm::vec4& outlineColor     = glm::vec4(0.0f),
                                  float            outlineThickness = 0.0f);

            CameraInfo& getCameraInfo() { return m_CameraInfo; }
            LightInfo&  getLightInfo() { return m_LightInfo; }

            void setScene(LogicScene* scene) override;

            void                   setSettings(const BuiltinRenderSettings& settings) { m_Settings = settings; }
            BuiltinRenderSettings& getSettings() { return m_Settings; }

        private:
            void setupSamplers();

            void onImGuiRasterization();
            void onImGuiRayTracing();

            void renderRasterization(rhi::CommandBuffer& cb, rhi::Texture* renderTarget, const fsec dt);
            void renderRayTracing(rhi::CommandBuffer& cb, rhi::Texture* renderTarget, const fsec dt);

            void clearUIDrawList();
            void renderUIDrawList(rhi::CommandBuffer& cb);

        private:
            framegraph::Samplers           m_Samplers;
            framegraph::TransientResources m_TransientResources;

            LogicScene* m_LogicScene {nullptr};

            rhi::Swapchain::Format m_SwapChainFormat;

            CameraInfo m_CameraInfo {};
            FrameInfo  m_FrameInfo {};
            LightInfo  m_LightInfo {};

            CameraInfo m_XrCameraLeft {};
            CameraInfo m_XrCameraRight {};

            GBufferPass*          m_GBufferPass {nullptr};
            DeferredLightingPass* m_DeferredLightingPass {nullptr};
            SkyboxPass*           m_SkyboxPass {nullptr};
            ToneMappingPass*      m_ToneMappingPass {nullptr};
            GammaCorrectionPass*  m_GammaCorrectionPass {nullptr};
            FXAAPass*             m_FXAAPass {nullptr};
            FinalPass*            m_FinalPass {nullptr};
            BlitPass*             m_BlitPass {nullptr};

            CubemapConverter  m_CubemapConverter;
            Ref<rhi::Texture> m_Cubemap {nullptr};

            IBLDataGenerator  m_IBLDataGenerator;
            Ref<rhi::Texture> m_BrdfLUT {nullptr};
            Ref<rhi::Texture> m_IrradianceMap {nullptr};
            Ref<rhi::Texture> m_PrefilteredEnvMap {nullptr};

            bool m_EnableSkybox {false};

            BuiltinRenderSettings m_Settings {};

            glm::vec4 m_ClearColor {0.0f, 0.0f, 0.0f, 1.0f};

            UIDrawList m_UIDrawList;
            UIPass*    m_UIPass {nullptr};

            SimpleRaytracingPass* m_SimpleRaytracingPass {nullptr};

            std::vector<Ref<DefaultMesh>> m_AreaLightMeshResources; // Keep alive for raytracing purposes
        };
    } // namespace gfx
} // namespace vultra