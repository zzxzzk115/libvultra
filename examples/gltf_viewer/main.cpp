#include "vultra/function/scenegraph/logic_scene.hpp"
#include <vultra/core/base/common_context.hpp>
#include <vultra/function/app/imgui_app.hpp>
#include <vultra/function/renderer/builtin/builtin_renderer.hpp>
#include <vultra/function/scenegraph/entity.hpp>

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
        auto  camera                = m_LogicScene.createMainCamera();
        auto& camTransform          = camera.getComponent<TransformComponent>();
        auto& camComponent          = camera.getComponent<CameraComponent>();
        camTransform.position       = glm::vec3(0.0f, 0.0f, 5.0f);
        camComponent.viewPortWidth  = m_Window.getExtent().x;
        camComponent.viewPortHeight = m_Window.getExtent().y;

        m_LogicScene.createMeshEntity("Damaged Helmet", "resources/models/DamagedHelmet/DamagedHelmet.gltf");
        m_Renderer.setScene(&m_LogicScene);
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
    gfx::BuiltinRenderer m_Renderer;
    LogicScene           m_LogicScene {"GLTF Viewer Scene"};
};

CONFIG_MAIN(GLTFViewerApp)