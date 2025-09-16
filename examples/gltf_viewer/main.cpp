#include "vultra/function/renderer/mesh_manager.hpp"
#include <vultra/core/base/common_context.hpp>
#include <vultra/function/app/imgui_app.hpp>
#include <vultra/function/renderer/builtin/builtin_renderer.hpp>

#include <imgui.h>

#include <glm/gtc/matrix_transform.hpp>

using namespace vultra;

class GLTFViewerApp final : public ImGuiApp
{
public:
    explicit GLTFViewerApp(const std::span<char*>& args) :
        ImGuiApp(args, {.title = "GLTF Viewer", .vSyncConfig = rhi::VerticalSync::eEnabled}, {.enableDocking = false}),
        m_Renderer(*m_RenderDevice)
    {
        m_MeshResource = resource::loadResource<gfx::MeshManager>("resources/models/DamagedHelmet/DamagedHelmet.gltf");

        std::vector<gfx::Renderable> renderables;
        renderables.push_back({.mesh = m_MeshResource});
        m_Renderer.setRenderables(renderables);

        auto& camInfo = m_Renderer.getCameraInfo();
        camInfo.zNear = 0.1f;
        camInfo.zFar  = 100.0f;
        camInfo.view =
            glm::lookAt(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        camInfo.projection = glm::perspectiveFov(45.0f,
                                                 static_cast<float>(m_Window.getExtent().x),
                                                 static_cast<float>(m_Window.getExtent().y),
                                                 camInfo.zNear,
                                                 camInfo.zFar);
        camInfo.projection[1][1] *= -1; // Flip Y for Vulkan
        camInfo.viewProjection            = camInfo.projection * camInfo.view;
        camInfo.inverseOriginalProjection = glm::inverse(camInfo.projection);
    }

    void onImGui() override
    {
        ImGui::Begin("GLTF Viewer Settings");
#ifdef VULTRA_ENABLE_RENDERDOC
        ImGui::Button("Capture One Frame");
        if (ImGui::IsItemClicked())
        {
            m_WantCaptureFrame = true;
        }
#endif
        ImGui::End();
    }

    void onRender(rhi::CommandBuffer& cb, const rhi::RenderTargetView rtv, const fsec dt) override
    {
        const auto& [frameIndex, target] = rtv;
        m_Renderer.render(cb, &target, dt);
        ImGuiApp::onRender(cb, rtv, dt);
    }

private:
    gfx::BuiltinRenderer   m_Renderer;
    Ref<gfx::MeshResource> m_MeshResource {nullptr};
};

CONFIG_MAIN(GLTFViewerApp)