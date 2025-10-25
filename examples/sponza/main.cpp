#include <vultra/core/base/common_context.hpp>
#include <vultra/core/input/input.hpp>
#include <vultra/function/app/imgui_app.hpp>
#include <vultra/function/camera/fps_camera.hpp>
#include <vultra/function/renderer/builtin/builtin_renderer.hpp>
#include <vultra/function/renderer/builtin/pass_output_mode.hpp>
#include <vultra/function/renderer/imgui_renderer.hpp>
#include <vultra/function/scenegraph/entity.hpp>
#include <vultra/function/scenegraph/logic_scene.hpp>

#include <imgui.h>

#include <glm/gtc/matrix_transform.hpp>

using namespace vultra;

const char* MODEL_ENTITY_NAME = "Sponza";
const char* MODEL_PATH        = "resources/models/Sponza/Sponza.gltf";
const char* ENV_MAP_PATH      = "resources/textures/environment_maps/citrus_orchard_puresky_1k.hdr";

class SponzaApp final : public ImGuiApp
{
public:
    explicit SponzaApp(const std::span<char*>& args) :
        ImGuiApp(args,
                 {.title = "Sponza",
                  .renderDeviceFeatureFlag =
                      rhi::RenderDeviceFeatureFlagBits::eRayTracing | rhi::RenderDeviceFeatureFlagBits::eMeshShader},
                 {.enableDocking = false}),
        m_Renderer(*m_RenderDevice, m_Swapchain.getFormat())
    {
        // Render Settings
        // As the Sponza scene is in-door, we disable IBL by default
        m_Renderer.getSettings().enableIBL = false;

        // Setup scene

        // Main Camera
        auto  camera          = m_LogicScene.createMainCamera();
        auto& camTransform    = camera.getComponent<TransformComponent>();
        auto& camComponent    = camera.getComponent<CameraComponent>();
        camTransform.position = glm::vec3(8.0f, 1.5f, -0.5f);
        camTransform.setRotationEuler({0.0f, 90.0f, 0.0f});
        camComponent.viewPortWidth      = m_Window.getExtent().x;
        camComponent.viewPortHeight     = m_Window.getExtent().y;
        camComponent.clearFlags         = CameraClearFlags::eSkybox;
        camComponent.environmentMapPath = ENV_MAP_PATH;

        // First Person Shooter Camera Controller
        m_FPSCamera = createScope<FirstPersonShooterCamera>(&camTransform);

        // Point Light
        auto  pointLight              = m_LogicScene.createPointLight();
        auto& pointLightTransform     = pointLight.getComponent<TransformComponent>();
        pointLightTransform.position  = glm::vec3(-8.0f, 2.0f, -0.5f);
        auto& pointLightComponent     = pointLight.getComponent<PointLightComponent>();
        pointLightComponent.radius    = 5.0f;
        pointLightComponent.intensity = 50.0f;
        pointLightComponent.color     = glm::vec3(0.9f, 0.9f, 0.1f);

        // Area Light (RED)
        auto  areaLightRed              = m_LogicScene.createAreaLight();
        auto& areaRedLightTransform     = areaLightRed.getComponent<TransformComponent>();
        areaRedLightTransform.position  = glm::vec3(-2.0f, 1.0f, 0.8f);
        auto& areaRedLightComponent     = areaLightRed.getComponent<AreaLightComponent>();
        areaRedLightComponent.color     = glm::vec3(0.9f, 0.1f, 0.1f);
        areaRedLightComponent.intensity = 10.0f;

        // Area Light (GREEN)
        auto  areaLightGreen              = m_LogicScene.createAreaLight();
        auto& areaGreenLightTransform     = areaLightGreen.getComponent<TransformComponent>();
        areaGreenLightTransform.position  = glm::vec3(0.0f, 1.0f, 0.8f);
        auto& areaGreenLightComponent     = areaLightGreen.getComponent<AreaLightComponent>();
        areaGreenLightComponent.color     = glm::vec3(0.1f, 0.9f, 0.1f);
        areaGreenLightComponent.intensity = 10.0f;

        // Area Light (BLUE)
        auto  areaLightBlue              = m_LogicScene.createAreaLight();
        auto& areaBlueLightTransform     = areaLightBlue.getComponent<TransformComponent>();
        areaBlueLightTransform.position  = glm::vec3(-4.0f, 1.0f, 0.8f);
        auto& areaBlueLightComponent     = areaLightBlue.getComponent<AreaLightComponent>();
        areaBlueLightComponent.color     = glm::vec3(0.1f, 0.1f, 0.9f);
        areaBlueLightComponent.intensity = 10.0f;

        // Load a sample model
        auto model = m_LogicScene.createRawMeshEntity(MODEL_ENTITY_NAME, MODEL_PATH);

        // Set camera far plane based on model's AABB
        auto& rawMesh     = model.getComponent<RawMeshComponent>().mesh;
        camComponent.zFar = rawMesh->aabb.getRadius() * 2.0f;
    }

    void onImGui() override
    {
        ImGui::Begin("Sponza Example");
        m_FPSCamera->enableCameraControl(!ImGui::IsWindowHovered());

        ImGuiExt::Combo("Renderer Type", m_Renderer.getSettings().rendererType);

        m_Renderer.onImGui();

        m_FPSCamera->onImGui();

#ifdef VULTRA_ENABLE_RENDERDOC
        ImGui::Button("Capture One Frame");
        if (ImGui::IsItemClicked())
        {
            m_WantCaptureFrame = true;
        }
#endif
        ImGui::End();
    }

    void onUpdate(const fsec dt) override
    {
        // Close on Escape
        if (Input::getKeyDown(KeyCode::eEscape))
        {
            close();
        }

        m_FPSCamera->onUpdate(dt);

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

CONFIG_MAIN(SponzaApp)