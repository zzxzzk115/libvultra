#include "vultra/core/app/demo_app_host.hpp"
#include "vultra/core/base/base.hpp"
#include "vultra/core/base/common_context.hpp"
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
#include "vultra/function/scripting/script_system.hpp"
#include "vultra/function/services/render_backend_service.hpp"
#include "vultra/function/services/render_service.hpp"
#include "vultra/function/world/world_system.hpp"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>

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

        [[nodiscard]] std::optional<rhi::RenderBackendApi> parseBackendToken(const std::string_view token)
        {
            if (token == "vulkan" || token == "vk")
            {
                return rhi::RenderBackendApi::eVulkan;
            }
            if (token == "webgpu" || token == "wgpu")
            {
                return rhi::RenderBackendApi::eWebGPU;
            }
            if (token == "auto")
            {
                return rhi::RenderBackendApi::eAuto;
            }
            return std::nullopt;
        }

        [[nodiscard]] std::optional<UniversalRenderer::RenderPath> parseRenderProfileToken(const std::string_view token)
        {
            if (token == "default" || token == "universal")
            {
                return UniversalRenderer::RenderPath::eDefault;
            }
            if (token == "compat" || token == "compatibility")
            {
                return UniversalRenderer::RenderPath::eCompatibility;
            }
            return std::nullopt;
        }

        [[nodiscard]] std::optional<rhi::RenderBackendApi>
        parseCliBackend(std::span<const std::string> args, bool& sawBackendArg, bool& invalidBackendValue)
        {
            std::optional<rhi::RenderBackendApi> parsed;
            for (size_t i = 0; i < args.size(); ++i)
            {
                const std::string_view     arg = args[i];
                constexpr std::string_view kBackendEqPrefix {"--backend="};
                constexpr std::string_view kRenderBackendEqPrefix {"--render-backend="};

                if (arg.starts_with(kBackendEqPrefix))
                {
                    sawBackendArg = true;
                    if (auto value = parseBackendToken(arg.substr(kBackendEqPrefix.size())); value.has_value())
                    {
                        parsed = value;
                    }
                    else
                    {
                        invalidBackendValue = true;
                    }
                    continue;
                }
                if (arg.starts_with(kRenderBackendEqPrefix))
                {
                    sawBackendArg = true;
                    if (auto value = parseBackendToken(arg.substr(kRenderBackendEqPrefix.size())); value.has_value())
                    {
                        parsed = value;
                    }
                    else
                    {
                        invalidBackendValue = true;
                    }
                    continue;
                }
                if (arg == "--backend" || arg == "--render-backend")
                {
                    sawBackendArg = true;
                    if ((i + 1) < args.size())
                    {
                        if (auto value = parseBackendToken(args[i + 1]); value.has_value())
                        {
                            parsed = value;
                        }
                        else
                        {
                            invalidBackendValue = true;
                        }
                        ++i;
                    }
                    else
                    {
                        invalidBackendValue = true;
                    }
                }
            }
            return parsed;
        }

        [[nodiscard]] std::optional<UniversalRenderer::RenderPath>
        parseCliRenderProfile(std::span<const std::string> args, bool& sawRenderProfileArg, bool& invalidRenderProfileValue)
        {
            std::optional<UniversalRenderer::RenderPath> parsed;
            for (size_t i = 0; i < args.size(); ++i)
            {
                const std::string_view arg = args[i];
                constexpr std::string_view kRenderProfileEqPrefix {"--render-profile="};

                if (arg.starts_with(kRenderProfileEqPrefix))
                {
                    sawRenderProfileArg = true;
                    if (auto value = parseRenderProfileToken(arg.substr(kRenderProfileEqPrefix.size())); value.has_value())
                    {
                        parsed = value;
                    }
                    else
                    {
                        invalidRenderProfileValue = true;
                    }
                    continue;
                }

                if (arg == "--render-profile")
                {
                    sawRenderProfileArg = true;
                    if ((i + 1) < args.size())
                    {
                        if (auto value = parseRenderProfileToken(args[i + 1]); value.has_value())
                        {
                            parsed = value;
                        }
                        else
                        {
                            invalidRenderProfileValue = true;
                        }
                        ++i;
                    }
                    else
                    {
                        invalidRenderProfileValue = true;
                    }
                    continue;
                }
            }
            return parsed;
        }
    } // namespace

#if defined(__ANDROID__)
    void DemoAppHost::setAndroidRuntimeContext(const platform::android::AndroidAppRuntimeContext& runtimeContext)
    {
        m_AndroidRuntimeContext = runtimeContext;
    }
#endif

    FPSCameraController DemoAppHost::makeFPSCameraController() const
    {
        FPSCameraController controller {};
        controller.enabled                = true;
        controller.captureMouse           = true;
        controller.orbitDistance          = 3.0f;
        controller.orbitRotateSensitivity = 0.08f;
        controller.orbitPanSensitivity    = 0.002f;
        controller.orbitZoomSpeed         = 0.03f;
        controller.orbitPivot             = {0.0f, 1.0f, 0.0f};
        controller.moveSpeed              = 4.0f;
        controller.sprintMultiplier       = 2.5f;
        controller.mouseSensitivity       = 0.08f;
        controller.position               = {0.0f, 1.0f, 1.5f};
        controller.yawDegrees             = -90.0f;
        controller.pitchDegrees           = -20.0f;
        controller.fovYDegrees            = 60.0f;
        controller.zNear                  = 0.1f;
        controller.zFar                   = 1000.0f;
        return controller;
    }

    Ref<Renderer> DemoAppHost::makeRenderer() const { return createRef<UniversalRenderer>(); }

    void DemoAppHost::onConfigure(Engine& engine)
    {
        auto backendApi = demoRenderBackendApi();
        if (demoAllowCliBackendOverride())
        {
            bool sawBackendArg       = false;
            bool invalidBackendValue = false;
            if (auto parsed = parseCliBackend(commandLineArgs(), sawBackendArg, invalidBackendValue);
                parsed.has_value())
            {
                backendApi = *parsed;
            }

            if (invalidBackendValue)
            {
                VULTRA_CORE_WARN("[DemoAppHost] Invalid backend CLI value. Use --backend=(auto|vulkan|webgpu) or "
                                 "--render-backend=(...)");
            }
        }

        UniversalRenderer::RenderPath universalRenderPath = UniversalRenderer::RenderPath::eDefault;
        {
            bool sawRenderProfileArg       = false;
            bool invalidRenderProfileValue = false;
            if (auto parsed = parseCliRenderProfile(commandLineArgs(), sawRenderProfileArg, invalidRenderProfileValue);
                parsed.has_value())
            {
                universalRenderPath = *parsed;
            }

            if (invalidRenderProfileValue)
            {
                VULTRA_CORE_WARN(
                    "[DemoAppHost] Invalid render profile CLI value. Use --render-profile=(default|universal|compat|compatibility)");
            }
        }

        engine.ctx().config.window.title                   = demoWindowTitle();
        engine.ctx().config.window.resizable               = demoWindowResizable();
        engine.ctx().config.render.backendApi              = backendApi;
        engine.ctx().config.render.renderDeviceFeatureFlag = demoRenderDeviceFeatureFlag();
        engine.ctx().config.render.builtinShaderLibrary =
            universalRenderPath == UniversalRenderer::RenderPath::eCompatibility ?
                EngineContext::Config::RenderConfig::BuiltinShaderLibrary::eCompatibility :
                EngineContext::Config::RenderConfig::BuiltinShaderLibrary::eAuto;

#if defined(__EMSCRIPTEN__)
        // Wasm bundles resources.vpk via --preload-file and loads assets from VPK by default.
        engine.ctx().config.asset.assetRoot   = "/";
        engine.ctx().config.asset.vpkFile     = "resources.vpk";
        engine.ctx().config.asset.loadFromVPK = true;
        // Keep ImGui ini path deterministic in wasm FS.
        engine.ctx().config.writableRoot = "/";
#endif

#if defined(__ANDROID__)
        if (m_AndroidRuntimeContext.has_value())
        {
            engine.ctx().config.window.android.app              = m_AndroidRuntimeContext->app;
            engine.ctx().config.window.android.nativeWindow     = m_AndroidRuntimeContext->nativeWindow;
            engine.ctx().config.window.android.destroyRequested = m_AndroidRuntimeContext->destroyRequested;
            engine.ctx().config.imgui.enableMultiview           = false;

            if (m_AndroidRuntimeContext->internalDataPath != nullptr &&
                m_AndroidRuntimeContext->internalDataPath[0] != '\0')
            {
                const auto basePath                 = std::filesystem::path(m_AndroidRuntimeContext->internalDataPath);
                engine.ctx().config.asset.assetRoot = basePath.generic_string();
                engine.ctx().config.writableRoot    = basePath.generic_string();

                if (m_AndroidRuntimeContext->useBundledVPK)
                {
                    engine.ctx().config.asset.vpkFile = (basePath / "resources.vpk").generic_string();
                }
            }

            engine.ctx().config.asset.loadFromVPK = m_AndroidRuntimeContext->useBundledVPK;
        }
#endif

        engine.emplaceSubsystem<WindowSystem>();
        engine.emplaceSubsystem<InputSystem>();
        engine.emplaceSubsystem<TimingSystem>();

        auto renderer = makeRenderer();
        if (!renderer)
        {
            renderer = createRef<UniversalRenderer>();
        }
        if (auto universalRenderer = std::dynamic_pointer_cast<UniversalRenderer>(renderer))
        {
            universalRenderer->setRenderPath(universalRenderPath);
        }

        auto& cameraSystem  = engine.emplaceSubsystem<CameraSystem>();
        auto  fpsController = makeFPSCameraController();
        auto& camera        = cameraSystem.addManualCamera({.rendererKey = renderer->name().data()});

        const float width  = static_cast<float>(std::max(engine.ctx().config.window.width, 1u));
        const float height = static_cast<float>(std::max(engine.ctx().config.window.height, 1u));
        const float aspect = width / height;

        const glm::vec3 forward = makeForward(fpsController.yawDegrees, fpsController.pitchDegrees);
        camera.view       = glm::lookAt(fpsController.position, fpsController.position + forward, glm::vec3(0, 1, 0));
        camera.projection = glm::perspectiveRH_ZO(
            glm::radians(fpsController.fovYDegrees), aspect, fpsController.zNear, fpsController.zFar);
        camera.fovY  = glm::radians(fpsController.fovYDegrees);
        camera.zNear = fpsController.zNear;
        camera.zFar  = fpsController.zFar;

        cameraSystem.setFPSCameraController(fpsController);

        engine.emplaceSubsystem<WorldSystem>();

#if !defined(__EMSCRIPTEN__)
        engine.emplaceSubsystem<FrameDebuggerSystem>();
#endif
        engine.emplaceSubsystem<ShaderSystem>();
        engine.emplaceSubsystem<RenderBackendSystem>();
        engine.emplaceSubsystem<ImGuiSystem>();

        const bool webgpuSafeMode =
            (backendApi == rhi::RenderBackendApi::eWebGPU) && !demoEnableExperimentalWebGPUContent();
        if (webgpuSafeMode)
        {
            VULTRA_CORE_WARN("[DemoAppHost] WebGPU safe mode is enabled: skipping render/asset/scene/script systems to "
                             "avoid unstable paths");
        }
        else
        {
            auto& renderSystem = engine.emplaceSubsystem<RenderSystem>();
            renderSystem.registerRenderer(renderer);

            engine.emplaceSubsystem<GpuResourceSystem>();
            engine.emplaceSubsystem<AssetSystem>();
            engine.emplaceSubsystem<SceneSystem>();
            engine.emplaceSubsystem<ScriptSystem>();
        }

        onConfigureDemo(engine);
    }

    void DemoAppHost::onPostConfigure(Engine& engine)
    {
        auto& window = engine.ctx().services.require<IWindowService>().window();
        window.on<os::GeneralWindowEvent>([this](const os::GeneralWindowEvent& e, os::Window&) { onWindowEvent(e); });

        const bool webgpuSafeMode = (engine.ctx().config.render.backendApi == rhi::RenderBackendApi::eWebGPU) &&
                                    !demoEnableExperimentalWebGPUContent();
        if (!webgpuSafeMode)
        {
            onPostConfigureDemo(engine);
        }
    }

    void DemoAppHost::onWindowEvent(const os::GeneralWindowEvent& e)
    {
        engineCtx().services.require<IInputService>().handleEvent(e);
        engineCtx().services.require<IImGuiService>().processEvent(e);

        if (e.type == event::WindowEventType::eResized)
        {
            auto* backendService = engineCtx().services.tryGet<IRenderBackendService>();
            auto* windowService  = engineCtx().services.tryGet<IWindowService>();
            if (backendService == nullptr || windowService == nullptr)
            {
                return;
            }

            const auto     framebufferExtent = windowService->window().getFrameBufferExtent();
            const uint32_t framebufferWidth  = static_cast<uint32_t>(std::max(framebufferExtent.x, 0));
            const uint32_t framebufferHeight = static_cast<uint32_t>(std::max(framebufferExtent.y, 0));
            if (framebufferWidth == 0u || framebufferHeight == 0u)
            {
                return;
            }

            const auto swapchainExtent = backendService->swapchain().getExtent();
            if (swapchainExtent.width == framebufferWidth && swapchainExtent.height == framebufferHeight)
            {
                return;
            }

            backendService->renderDevice().waitIdle();
            backendService->frameController().recreate();

            if (auto* renderService = engineCtx().services.tryGet<IRenderService>(); renderService != nullptr)
            {
                renderService->onResize(framebufferWidth, framebufferHeight);
            }
        }
    }

    void DemoAppHost::onPollEvents()
    {
        auto&     window        = engineCtx().services.require<IWindowService>().window();
        const int timeoutMillis = (!window.isReady()) ? -1 : 0;
        window.pollEvents(timeoutMillis);

        auto& input = engineCtx().services.require<IInputService>();
        if (input.getKeyDown(KeyCode::eEscape))
            window.close();
    }

    bool DemoAppHost::onShouldClose() const
    {
        auto& window               = engineCtx().services.require<IWindowService>().window();
        auto* renderBackendService = engineCtx().services.tryGet<IRenderBackendService>();
        return window.shouldClose() || (renderBackendService && renderBackendService->isExitRequested());
    }
} // namespace vultra
