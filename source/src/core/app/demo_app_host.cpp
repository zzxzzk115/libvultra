#include "vultra/core/app/demo_app_host.hpp"
#include "vultra/core/base/base.hpp"
#include "vultra/core/input/input_system.hpp"
#include "vultra/core/os/window_system.hpp"
#include "vultra/core/services/input_service.hpp"
#include "vultra/core/services/window_service.hpp"
#include "vultra/core/timing/timing_system.hpp"
#include "vultra/function/asset/asset_system.hpp"
#include "vultra/function/camera/camera_system.hpp"
#include "vultra/function/debugging/frame_debugger_system.hpp"
#include "vultra/function/imgui/imgui_system.hpp"
#include "vultra/function/rendering/backend/render_backend_system.hpp"
#include "vultra/function/rendering/render_system.hpp"
#include "vultra/function/rendering/shader_system.hpp"
#include "vultra/function/rendering/srp/builtin/universal_renderer.hpp"
#include "vultra/function/resource/gpu_resource_system.hpp"
#include "vultra/function/scene/scene_system.hpp"
#include "vultra/function/world/world_system.hpp"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>

namespace vultra
{
    namespace
    {
        glm::vec3 makeForward(float yawDegrees, float pitchDegrees)
        {
            const float yawRad   = glm::radians(yawDegrees);
            const float pitchRad = glm::radians(pitchDegrees);

            return glm::normalize(glm::vec3 {
                std::cos(yawRad) * std::cos(pitchRad),
                std::sin(pitchRad),
                std::sin(yawRad) * std::cos(pitchRad),
            });
        }
    } // namespace

    FPSCameraController DemoAppHost::makeFPSCameraController() const
    {
        FPSCameraController controller {};
        controller.enabled          = false;
        controller.captureMouse     = true;
        controller.moveSpeed        = 4.0f;
        controller.sprintMultiplier = 2.5f;
        controller.mouseSensitivity = 0.08f;
        controller.position         = {0.0f, 1.0f, 1.5f};
        controller.yawDegrees       = -90.0f;
        controller.pitchDegrees     = -20.0f;
        controller.fovYDegrees      = 60.0f;
        controller.zNear            = 0.1f;
        controller.zFar             = 100.0f;
        return controller;
    }

    Ref<Renderer> DemoAppHost::makeRenderer() const { return createRef<UniversalRenderer>(); }

    void DemoAppHost::onConfigure(Engine& engine)
    {
        engine.ctx().config.window.title                   = demoWindowTitle();
        engine.ctx().config.window.resizable               = demoWindowResizable();
        engine.ctx().config.render.renderDeviceFeatureFlag = demoRenderDeviceFeatureFlag();

        engine.emplaceSubsystem<WindowSystem>();
        engine.emplaceSubsystem<InputSystem>();
        engine.emplaceSubsystem<TimingSystem>();

        auto renderer = makeRenderer();
        if (!renderer)
        {
            renderer = createRef<UniversalRenderer>();
        }

        auto& cameraSystem  = engine.emplaceSubsystem<CameraSystem>();
        auto  fpsController = makeFPSCameraController();
        auto& camera        = cameraSystem.addManualCamera({.rendererKey = renderer->name().data()});

        const float width  = static_cast<float>(std::max(engine.ctx().config.window.width, 1u));
        const float height = static_cast<float>(std::max(engine.ctx().config.window.height, 1u));
        const float aspect = width / height;

        const glm::vec3 forward = makeForward(fpsController.yawDegrees, fpsController.pitchDegrees);
        camera.view = glm::lookAt(fpsController.position, fpsController.position + forward, glm::vec3(0, 1, 0));
        camera.projection =
            glm::perspective(glm::radians(fpsController.fovYDegrees), aspect, fpsController.zNear, fpsController.zFar);
        camera.projection[1][1] *= -1.0f; // Vulkan clip space adjustment
        camera.fovY  = glm::radians(fpsController.fovYDegrees);
        camera.zNear = fpsController.zNear;
        camera.zFar  = fpsController.zFar;

        cameraSystem.setFPSCameraController(fpsController);

        engine.emplaceSubsystem<WorldSystem>();

        engine.emplaceSubsystem<FrameDebuggerSystem>();
        engine.emplaceSubsystem<ShaderSystem>();
        engine.emplaceSubsystem<RenderBackendSystem>();
        engine.emplaceSubsystem<ImGuiSystem>();

        auto& renderSystem = engine.emplaceSubsystem<RenderSystem>();
        renderSystem.registerRenderer(renderer);

        engine.emplaceSubsystem<GpuResourceSystem>();
        engine.emplaceSubsystem<AssetSystem>();
        engine.emplaceSubsystem<SceneSystem>();

        onConfigureDemo(engine);
    }

    void DemoAppHost::onPostConfigure(Engine& engine)
    {
        auto& window = engine.ctx().services.require<IWindowService>().window();
        window.on<os::GeneralWindowEvent>([this](os::GeneralWindowEvent& e, os::Window&) { onWindowEvent(e); });

        onPostConfigureDemo(engine);
    }

    void DemoAppHost::onWindowEvent(const os::GeneralWindowEvent& e)
    {
        engineCtx().services.require<IInputService>().handleEvent(e.internalEvent);
        ImGuiSystem::processEvent(e);
    }

    void DemoAppHost::onPollEvents()
    {
        auto& window = engineCtx().services.require<IWindowService>().window();
        window.pollEvents();

        auto& input = engineCtx().services.require<IInputService>();
        if (input.getKeyDown(KeyCode::eEscape))
            window.close();
    }

    bool DemoAppHost::onShouldClose() const
    {
        auto& window = engineCtx().services.require<IWindowService>().window();
        return window.shouldClose();
    }
} // namespace vultra
