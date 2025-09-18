#include <vultra/core/base/common_context.hpp>
#include <vultra/core/input/input.hpp>
#include <vultra/function/app/imgui_app.hpp>
#include <vultra/function/renderer/builtin/builtin_renderer.hpp>
#include <vultra/function/renderer/builtin/pass_output_mode.hpp>
#include <vultra/function/scenegraph/entity.hpp>
#include <vultra/function/scenegraph/logic_scene.hpp>

#include <imgui.h>

#include <glm/gtc/matrix_transform.hpp>

using namespace vultra;

const char* MODEL_ENTITY_NAME = "Damaged Helmet";
const char* MODEL_PATH        = "resources/models/DamagedHelmet/DamagedHelmet.gltf";

class GLTFViewerApp final : public ImGuiApp
{
public:
    explicit GLTFViewerApp(const std::span<char*>& args) :
        ImGuiApp(args, {.title = "GLTF Viewer", .vSyncConfig = rhi::VerticalSync::eEnabled}, {.enableDocking = false}),
        m_Renderer(*m_RenderDevice)
    {
        // Setup scene

        // Main Camera
        auto  camera                = m_LogicScene.createMainCamera();
        auto& camTransform          = camera.getComponent<TransformComponent>();
        auto& camComponent          = camera.getComponent<CameraComponent>();
        camTransform.position       = glm::vec3(0.0f, 0.0f, 5.0f);
        camComponent.viewPortWidth  = m_Window.getExtent().x;
        camComponent.viewPortHeight = m_Window.getExtent().y;

        // Directional Light
        auto  directionalLight   = m_LogicScene.createDirectionalLight();
        auto& lightComponent     = directionalLight.getComponent<DirectionalLightComponent>();
        lightComponent.direction = glm::normalize(glm::vec3(0.0f, 0.0f, -1.0f));

        // Load a sample model
        auto model = m_LogicScene.createMeshEntity(MODEL_ENTITY_NAME, MODEL_PATH);

        // Set camera far plane based on model's AABB
        auto& rawMesh     = model.getComponent<RawMeshComponent>().mesh;
        camComponent.zFar = rawMesh->aabb.getRadius() * 10.0f;
    }

    void onImGui() override
    {
        ImGui::Begin("GLTF Viewer");
        m_EnableOrbitCamera = !ImGui::IsItemHovered() && !ImGui::IsAnyItemActive();

        auto& settings   = m_Renderer.getSettings();
        int   outputMode = static_cast<int>(settings.outputMode);
        ImGui::RadioButton("Albedo", &outputMode, static_cast<int>(gfx::PassOutputMode::Albedo));
        ImGui::RadioButton("Normal", &outputMode, static_cast<int>(gfx::PassOutputMode::Normal));
        ImGui::RadioButton("Emissive", &outputMode, static_cast<int>(gfx::PassOutputMode::Emissive));
        ImGui::RadioButton("Metallic", &outputMode, static_cast<int>(gfx::PassOutputMode::Metallic));
        ImGui::RadioButton("Roughness", &outputMode, static_cast<int>(gfx::PassOutputMode::Roughness));
        ImGui::RadioButton("Ambient Occlusion", &outputMode, static_cast<int>(gfx::PassOutputMode::AmbientOcclusion));
        ImGui::RadioButton("Depth", &outputMode, static_cast<int>(gfx::PassOutputMode::Depth));
        ImGui::RadioButton("SceneColor (HDR)", &outputMode, static_cast<int>(gfx::PassOutputMode::SceneColor_HDR));
        ImGui::RadioButton("SceneColor (LDR)", &outputMode, static_cast<int>(gfx::PassOutputMode::SceneColor_LDR));
        ImGui::RadioButton(
            "SceneColor (Anti-Aliased)", &outputMode, static_cast<int>(gfx::PassOutputMode::SceneColor_AntiAliased));
        settings.outputMode = static_cast<gfx::PassOutputMode>(outputMode);

        ImGui::Checkbox("Enable Normal Mapping", &settings.enableNormalMapping);

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

        // Mouse orbit camera
        static glm::vec2 lastMousePos = Input::getMousePosition();

        if (m_EnableOrbitCamera)
        {
            // Left button drag to rotate mesh
            if (Input::getMouseButton(MouseCode::eLeft))
            {
                const auto mousePos = Input::getMousePosition();
                const auto delta    = mousePos - lastMousePos;
                lastMousePos        = mousePos;

                auto& meshTransform =
                    m_LogicScene.getEntityWithName(MODEL_ENTITY_NAME).getComponent<TransformComponent>();
                auto euler = meshTransform.getRotationEuler();
                euler.x += delta.y * 0.1f;
                euler.y += delta.x * 0.1f;
                euler.x = glm::clamp(euler.x, -89.0f, 89.0f);
                meshTransform.setRotationEuler(euler);
            }
            else
            {
                // Reset last mouse position when not dragging
                if (Input::getMouseButtonDown(MouseCode::eLeft))
                {
                    lastMousePos = Input::getMousePosition();
                }
            }

            // Right button drag to zoom in/out
            static bool wasPressed = false;
            if (Input::getMouseButton(MouseCode::eRight))
            {
                const auto mousePos = Input::getMousePosition();
                const auto delta    = lastMousePos - mousePos;
                lastMousePos        = mousePos;

                auto& camTransform = m_LogicScene.getMainCamera().getComponent<TransformComponent>();
                camTransform.position += camTransform.forward() * (delta.y * 0.01f);
            }
            else
            {
                // Reset last mouse position when not dragging
                if (wasPressed)
                {
                    wasPressed = false;
                }
                else
                {
                    lastMousePos = Input::getMousePosition();
                }
            }
        }
        else
        {
            // Sync mouse position when not orbiting
            lastMousePos = Input::getMousePosition();
        }

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
    LogicScene           m_LogicScene {"GLTF Viewer Scene"};

    bool m_EnableOrbitCamera {true};
};

CONFIG_MAIN(GLTFViewerApp)