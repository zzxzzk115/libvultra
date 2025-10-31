#include <vultra/core/base/common_context.hpp>
#include <vultra/core/input/input.hpp>
#include <vultra/function/app/imgui_app.hpp>
#include <vultra/function/camera/fps_camera.hpp>
#include <vultra/function/renderer/builtin/builtin_renderer.hpp>
#include <vultra/function/renderer/builtin/pass_output_mode.hpp>
#include <vultra/function/renderer/imgui_renderer.hpp>
#include <vultra/function/scenegraph/component_utils.hpp>
#include <vultra/function/scenegraph/entity.hpp>
#include <vultra/function/scenegraph/logic_scene.hpp>

#include <imgui.h>

#include <glm/gtc/matrix_transform.hpp>

using namespace vultra;

class DebugDrawApp final : public ImGuiApp
{
public:
    explicit DebugDrawApp(const std::span<char*>& args) :
        ImGuiApp(args,
                 {.title = "Debug Draw Example",
                  .renderDeviceFeatureFlag =
                      rhi::RenderDeviceFeatureFlagBits::eRayTracing | rhi::RenderDeviceFeatureFlagBits::eMeshShader},
                 {.enableDocking = false}),
        m_Renderer(*m_RenderDevice, m_Swapchain.getFormat())
    {
        // Setup scene

        // Main Camera
        auto  camera          = m_LogicScene.createMainCamera();
        auto& camTransform    = camera.getComponent<TransformComponent>();
        auto& camComponent    = camera.getComponent<CameraComponent>();
        camTransform.position = glm::vec3(-0.4665387, 0.7503508, 1.6851553);
        camTransform.setRotationEuler({-22.20004, -15.999853, 0});
        camComponent.viewPortWidth  = m_Window.getExtent().x;
        camComponent.viewPortHeight = m_Window.getExtent().y;
        camComponent.clearFlags     = CameraClearFlags::eColor;
        camComponent.clearColor     = glm::vec4(0.1f, 0.1f, 0.1f, 1.0f);

        // First Person Shooter Camera Controller
        m_FPSCamera = createScope<FirstPersonShooterCamera>(&camTransform);
    }

    void onImGui() override
    {
        ImGui::Begin("Debug Draw Example", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        m_FPSCamera->enableCameraControl(!ImGui::IsWindowHovered());

        // m_Renderer.onImGui();

        m_FPSCamera->onImGui();

#ifdef VULTRA_ENABLE_RENDERDOC
        ImGui::Button("Capture One Frame");
        if (ImGui::IsItemClicked())
        {
            m_WantCaptureFrame = true;
        }
#endif
        ImGui::End();

        auto& mainCamTransform = m_LogicScene.getMainCamera().getComponent<TransformComponent>();
        auto& mainCam          = m_LogicScene.getMainCamera().getComponent<CameraComponent>();

        auto viewMatrix = getCameraViewMatrix(mainCamTransform);
        auto projMatrix = getCameraProjectionMatrix(mainCam);
        auto viewProj   = projMatrix * viewMatrix;
        commonContext.debugDraw->setViewProjectionMatrix(viewProj);

        const ddVec3 boxColor  = {0.0f, 0.8f, 0.8f};
        const ddVec3 boxCenter = {0.0f, 0.0f, 0.0f};
        dd::box(boxCenter, boxColor, 0.5f, 0.5f, 0.5f);
        dd::cross(boxCenter, 0.2f);
    }

    void onUpdate(const fsec dt) override
    {
        // Close on Escape
        if (Input::getKeyDown(KeyCode::eEscape))
        {
            close();
        }

        m_FPSCamera->onUpdate(dt);

        VULTRA_CLIENT_INFO("Camera Position: {}, {}, {}",
                           m_FPSCamera->getPosition().x,
                           m_FPSCamera->getPosition().y,
                           m_FPSCamera->getPosition().z);
        VULTRA_CLIENT_INFO("Camera Rotation: {}, {}, {}",
                           m_FPSCamera->getRotationEuler().x,
                           m_FPSCamera->getRotationEuler().y,
                           m_FPSCamera->getRotationEuler().z);

        m_Renderer.setScene(&m_LogicScene);

        ImGuiApp::onUpdate(dt);
    }

    void onRender(rhi::CommandBuffer& cb, const rhi::RenderTargetView rtv, const fsec dt) override
    {
        const auto& [frameIndex, target] = rtv;
        m_Renderer.render(cb, &target, dt);
        ImGuiApp::onRender(cb, rtv, dt);
    }

    void onResize(uint32_t width, uint32_t height) override
    {
        auto& camComponent          = m_LogicScene.getMainCamera().getComponent<CameraComponent>();
        camComponent.viewPortWidth  = width;
        camComponent.viewPortHeight = height;

        ImGuiApp::onResize(width, height);
    }

private:
    gfx::BuiltinRenderer m_Renderer;
    LogicScene           m_LogicScene {"Sponza Scene"};

    Scope<FirstPersonShooterCamera> m_FPSCamera {nullptr};
};

CONFIG_MAIN(DebugDrawApp)