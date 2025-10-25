#include <vultra/core/base/common_context.hpp>
#include <vultra/core/input/input.hpp>
#include <vultra/core/rhi/graphics_pipeline.hpp>
#include <vultra/core/rhi/vertex_buffer.hpp>
#include <vultra/function/app/xr_app.hpp>
#include <vultra/function/openxr/ext/xr_eyetracker.hpp>
#include <vultra/function/openxr/xr_device.hpp>
#include <vultra/function/openxr/xr_headset.hpp>
#include <vultra/function/renderer/builtin/builtin_renderer.hpp>
#include <vultra/function/scenegraph/entity.hpp>
#include <vultra/function/scenegraph/logic_scene.hpp>

#include <imgui.h>

const char*     MODEL_ENTITY_NAME = "Sponza";
const char*     MODEL_PATH        = "resources/models/Sponza/Sponza.gltf";
const char*     ENV_MAP_PATH      = "resources/textures/environment_maps/citrus_orchard_puresky_1k.hdr";
const glm::vec4 FILL_COLOR        = glm::vec4(0.0f, 1.0f, 1.0f, 0.3f);
const glm::vec4 OUTLINE_COLOR     = glm::vec4(0.0f, 0.2f, 0.4f, 0.5f);

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
        auto  camera                      = m_LogicScene.createMainCamera();
        auto& cameraTransformComponent    = camera.getComponent<TransformComponent>();
        cameraTransformComponent.position = glm::vec3(8.0f, 1.5f, 0.0f);
        cameraTransformComponent.setRotationEuler(glm::vec3(0.0f, glm::radians(90.0f), 0.0f));
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
        auto model = m_LogicScene.createRawMeshEntity(MODEL_ENTITY_NAME, MODEL_PATH);
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

        const auto* eyeTracker = m_CommonAction.getEyeTracker();
        ImGui::Text("Eye Tracker Enabled : %s", eyeTracker ? "Yes" : "No");
        if (eyeTracker)
        {
            const auto& gazePose = eyeTracker->getGazePose();
            ImGui::Text(
                "Gaze Position : (%.3f, %.3f, %.3f)", gazePose.position.x, gazePose.position.y, gazePose.position.z);
            const auto gazeRotationQuat  = xrutils::toQuat(gazePose.orientation);
            const auto gazeRotationEuler = glm::eulerAngles(gazeRotationQuat);
            ImGui::Text("Gaze Rotation : (%.3f, %.3f, %.3f) (degrees)",
                        glm::degrees(gazeRotationEuler.x),
                        glm::degrees(gazeRotationEuler.y),
                        glm::degrees(gazeRotationEuler.z));
        }

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
        m_Renderer.beginFrame(cb);

        auto& [leftRTV, rightRTV] = xrRenderTargetView;
        m_Renderer.renderXR(cb, &leftRTV, &rightRTV, dt);

        // Render Eye Tracker Circle in screen space
        const auto* eyeTracker = m_CommonAction.getEyeTracker();
        if (eyeTracker)
        {
            auto& leftEyeCamera  = m_LogicScene.getXrCamera(true).getComponent<XrCameraComponent>();
            auto& rightEyeCamera = m_LogicScene.getXrCamera(false).getComponent<XrCameraComponent>();

            const auto& leftEyeViewMatrix = leftEyeCamera.viewMatrix;
            const auto& leftEyeProjMatrix = xrutils::createProjectionMatrix({.angleLeft  = leftEyeCamera.fovAngleLeft,
                                                                             .angleRight = leftEyeCamera.fovAngleRight,
                                                                             .angleUp    = leftEyeCamera.fovAngleUp,
                                                                             .angleDown  = leftEyeCamera.fovAngleDown},
                                                                            leftEyeCamera.zNear,
                                                                            leftEyeCamera.zFar);

            const auto& rightEyeViewMatrix = rightEyeCamera.viewMatrix;
            const auto& rightEyeProjMatrix =
                xrutils::createProjectionMatrix({.angleLeft  = rightEyeCamera.fovAngleLeft,
                                                 .angleRight = rightEyeCamera.fovAngleRight,
                                                 .angleUp    = rightEyeCamera.fovAngleUp,
                                                 .angleDown  = rightEyeCamera.fovAngleDown},
                                                rightEyeCamera.zNear,
                                                rightEyeCamera.zFar);

            const auto& gazePose = eyeTracker->getGazePose();
            if (gazePose.orientation.w != 0)
            {
                glm::vec3 origin  = xrutils::toVec3(gazePose.position);
                glm::vec3 forward = xrutils::toQuat(gazePose.orientation) * glm::vec3(0, 0, -1);

                const auto drawPerEyeCircle = [&](rhi::Texture&            target,
                                                  const glm::mat4&         viewMatrix,
                                                  const glm::mat4&         projMatrix,
                                                  const XrCameraComponent& eyeCamera,
                                                  float                    xOffset = 0.0f) {
                    // Raycast to one eye's near plane
                    glm::vec3 originVS = glm::vec3(viewMatrix * glm::vec4(origin, 1.0));
                    glm::vec3 dirVS    = glm::mat3(viewMatrix) * forward;

                    float     t     = -(originVS.z + eyeCamera.zNear) / dirVS.z;
                    glm::vec3 hitVS = originVS + t * dirVS;

                    // Convert to NDC
                    glm::vec4 clip = projMatrix * glm::vec4(hitVS, 1.0);
                    glm::vec3 ndc  = glm::vec3(clip) / clip.w;

                    // NDC to Screen Space
                    const auto& extent = target.getExtent();
                    glm::vec2   screenSpacePos {
                        ((ndc.x + xOffset) * 0.5f + 0.5f) * extent.width,
                        (ndc.y * 0.5f + 0.5f) * extent.height,
                    };

                    // Draw circle API
                    m_Renderer.drawCircleFilled(&target, screenSpacePos, 100.0f, FILL_COLOR, OUTLINE_COLOR, 5.0f);
                };

                // Left Eye
                drawPerEyeCircle(leftRTV, leftEyeViewMatrix, leftEyeProjMatrix, leftEyeCamera, -0.25f);

                // Right Eye
                drawPerEyeCircle(rightRTV, rightEyeViewMatrix, rightEyeProjMatrix, rightEyeCamera, 0.25f);
            }
        }

        m_Renderer.endFrame();
    }

private:
    gfx::BuiltinRenderer m_Renderer;

    LogicScene m_LogicScene {"OpenXR Sponza Scene"};
};

CONFIG_MAIN(OpenXRSponzaExampleApp)