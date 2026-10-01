#pragma once

#include "ray_scene.hpp"
#include "sample.hpp"

#include <vultra/drivers/rhi/shader_pipeline.hpp>
#include <vultra/main/app/imgui_app.hpp>
#include <vultra/scene/camera/fps_camera.hpp>
#include <vultra/servers/rendering/texture_blit.hpp>

namespace sample
{
    class RayTracingApp final : public vultra::ImGuiApp
    {
    public:
        RayTracingApp(const Options&               options,
                      const vultra::SceneData&     scene,
                      const std::filesystem::path& shader,
                      bool                         shadowRays,
                      const std::string&           title);
        ~RayTracingApp() override;

    private:
        void         release();
        VriPipeline* buildPipeline(std::span<const VriShaderDesc> shaders, bool shadowRays);
        void         onUpdate(float) override;
        void         onImGui() override;
        void         onPreRender() override;
        void         onRender(VriCommandBuffer* cmd, vultra::Texture& target) override;
        void         onPostRender(vultra::Texture& target) override;

        struct Parameters
        {
            glm::mat4 inverseViewProjection;
            glm::vec4 cameraPosition {0, 1, 4, 0};
            glm::vec4 missColor {0.2f, 0.3f, 0.3f, 1};
            glm::vec4 lightPositionArea;
            glm::vec4 lightNormal;
            glm::vec4 lightEmission;
        } m_Parameters {};

        static_assert(sizeof(Parameters) == 144);

        vultra::FpsCamera                       m_Camera;
        float                                   m_DeltaSeconds = 0;
        Options                                 m_Options;
        RayScene                                m_Scene;
        vultra::TextureBlit                     m_Blit;
        VriPipelineLayout*                      m_Layout        = nullptr;
        VriDescriptorPool*                      m_Pool          = nullptr;
        VriDescriptorSet*                       m_Set           = nullptr;
        VriDescriptor*                          m_ParameterView = nullptr;
        std::unique_ptr<vultra::Buffer>         m_ParameterBuffer;
        std::unique_ptr<vultra::Buffer>         m_Sbt;
        std::unique_ptr<vultra::Texture>        m_Output;
        std::unique_ptr<vultra::ShaderPipeline> m_Pipeline;
        uint64_t                                m_SbtStride = 0;
        uint32_t                                m_MissCount = 1;
    };
} // namespace sample
