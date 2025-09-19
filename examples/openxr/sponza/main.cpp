#include <vultra/core/base/common_context.hpp>
#include <vultra/core/input/input.hpp>
#include <vultra/core/rhi/graphics_pipeline.hpp>
#include <vultra/core/rhi/vertex_buffer.hpp>
#include <vultra/function/app/xr_app.hpp>
#include <vultra/function/openxr/xr_device.hpp>
#include <vultra/function/openxr/xr_headset.hpp>
#include <vultra/function/renderer/builtin/builtin_renderer.hpp>
#include <vultra/function/scenegraph/entity.hpp>
#include <vultra/function/scenegraph/logic_scene.hpp>

#include <imgui.h>

const char* MODEL_ENTITY_NAME = "Sponza";
const char* MODEL_PATH        = "resources/models/Sponza/Sponza.gltf";
const char* ENV_MAP_PATH      = "resources/textures/environment_maps/citrus_orchard_puresky_1k.hdr";

using namespace vultra;

class OpenXRSponzaExampleApp final : public XRApp
{
public:
    explicit OpenXRSponzaExampleApp(const std::span<char*>& args) :
        XRApp(args,
              {.title = "OpenXR Sponza Example", .renderDeviceFeatureFlag = rhi::RenderDeviceFeatureFlagBits::eOpenXR}),
        m_Renderer(*m_RenderDevice,
                   m_Headset.getSwapchainPixelFormat() == rhi::PixelFormat::eRGBA8_sRGB ?
                       rhi::Swapchain::Format::esRGB :
                       rhi::Swapchain::Format::eLinear)
    {
        // Setup scene

        // Main Camera, will be overridden by XR cameras
        // TODO: Move skybox logic to global <EnvironmentComponent>
        auto  camera                       = m_LogicScene.createMainCamera();
        auto& cameraComponent              = camera.getComponent<CameraComponent>();
        cameraComponent.clearFlags         = CameraClearFlags::eSkybox;
        cameraComponent.environmentMapPath = ENV_MAP_PATH;

        // Left Eye Camera
        m_LogicScene.createXrCamera(true);

        // Right Eye Camera
        m_LogicScene.createXrCamera(false);

        // Directional Light
        m_LogicScene.createDirectionalLight();

        // Load a sample model
        auto model = m_LogicScene.createMeshEntity(MODEL_ENTITY_NAME, MODEL_PATH);
    }

    void onImGui() override
    {
        ImGui::Begin("OpenXR Sponza Example");
        ImGui::Text("This is a simple OpenXR example that renders the Sponza scene.");

        const auto& xrDevice             = m_RenderDevice->getXRDevice();
        const auto& xrInstanceProperties = xrDevice->getXrInstanceProperties();
        ImGui::Text("OpenXR Runtime        : %s", xrInstanceProperties.runtimeName);
        ImGui::Text("OpenXR Runtime Version: %d.%d.%d",
                    XR_VERSION_MAJOR(xrInstanceProperties.runtimeVersion),
                    XR_VERSION_MINOR(xrInstanceProperties.runtimeVersion),
                    XR_VERSION_PATCH(xrInstanceProperties.runtimeVersion));

        m_Renderer.onImGui();

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

        // Override camera settings from XR headset
        auto& leftEyeCamera  = m_LogicScene.getXrCamera(true).getComponent<XrCameraComponent>();
        auto& rightEyeCamera = m_LogicScene.getXrCamera(false).getComponent<XrCameraComponent>();

        auto syncCamera = [&](XrCameraComponent& cam) {
            uint32_t eyeIndex = cam.isLeftEye ? 0 : 1;
            cam.position      = m_Headset.getEyePosition(eyeIndex);
            cam.rotation      = m_Headset.getEyeRotation(eyeIndex);
            cam.resolution    = m_Headset.getEyeResolution(eyeIndex);
            cam.viewMatrix    = m_Headset.getEyeViewMatrix(eyeIndex);
            cam.fovAngleLeft  = m_Headset.getEyeFOV(eyeIndex).angleLeft;
            cam.fovAngleRight = m_Headset.getEyeFOV(eyeIndex).angleRight;
            cam.fovAngleUp    = m_Headset.getEyeFOV(eyeIndex).angleUp;
            cam.fovAngleDown  = m_Headset.getEyeFOV(eyeIndex).angleDown;
        };

        syncCamera(leftEyeCamera);
        syncCamera(rightEyeCamera);

        m_Renderer.setScene(&m_LogicScene);

        ImGuiApp::onUpdate(dt);
    }

    void onXrRender(rhi::CommandBuffer&                        cb,
                    openxr::XRHeadset::StereoRenderTargetView& xrRenderTargetView,
                    const fsec                                 dt) override
    {
        auto& [leftRTV, rightRTV] = xrRenderTargetView;
        m_Renderer.renderXR(cb, &leftRTV, &rightRTV, dt);
    }

private:
    gfx::BuiltinRenderer m_Renderer;

    LogicScene m_LogicScene {"OpenXR Sponza Scene"};
};

CONFIG_MAIN(OpenXRSponzaExampleApp)