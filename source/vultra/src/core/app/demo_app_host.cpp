#include "vultra/core/app/demo_app_host.hpp"
#include "vultra/core/base/base.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/core/builtin/builtin_resources.hpp"
#include "vultra/core/i18n/i18n_system.hpp"
#include "vultra/core/input/input_system.hpp"
#include "vultra/core/os/window_system.hpp"
#include "vultra/core/services/input_service.hpp"
#include "vultra/core/services/window_service.hpp"
#include "vultra/core/timing/timing_system.hpp"
#include "vultra/function/asset/asset_system.hpp"
#include "vultra/function/animation/animation_system.hpp"
#include "vultra/function/audio/audio_system.hpp"
#include "vultra/function/camera/camera_system.hpp"
#include "vultra/function/debugging/frame_debugger_system.hpp"
#include "vultra/function/imgui/imgui_system.hpp"
#include "vultra/function/jobs/job_system.hpp"
#if defined(VULTRA_ENABLE_XR) && VULTRA_ENABLE_XR
#include "vultra/function/openxr/xr_runtime_system.hpp"
#endif
#include "vultra/function/particle/particle_system.hpp"
#include "vultra/function/physics/physics_system.hpp"
#include "vultra/function/plugin/plugin_system.hpp"
#include "vultra/function/rendering/backend/render_backend_extension_system.hpp"
#include "vultra/function/rendering/backend/render_backend_system.hpp"
#include "vultra/function/rendering/render_upscaler_system.hpp"
#include "vultra/function/rendering/render_system.hpp"
#include "vultra/function/rendering/shader_system.hpp"
#include "vultra/function/rendering/srp/builtin/universal_renderer.hpp"
#include "vultra/function/rendering/srp/builtin/universal_rt_renderer.hpp"
#include "vultra/function/resource/gpu_resource_system.hpp"
#include "vultra/function/scene/scene_system.hpp"
#include "vultra/function/scripting/script_system.hpp"
#include "vultra/function/services/render_backend_service.hpp"
#include "vultra/function/services/render_service.hpp"
#include "vultra/function/services/render_upscaler_service.hpp"
#include "vultra/function/ui/ui_system.hpp"
#include "vultra/function/world/world_system.hpp"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/trigonometric.hpp>

#include <vbase/core/scoped_enum_flags.hpp>

#if defined(__ANDROID__)
#include <android/asset_manager.h>
#include <android/log.h>

#include <cstddef>
#include <cstring>
#include <vector>
#endif

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

        [[nodiscard]] std::optional<UniversalRenderer::RenderProfile>
        parseRenderProfileToken(const std::string_view token)
        {
            if (token == "default" || token == "universal")
            {
                return UniversalRenderer::RenderProfile::eDefault;
            }
            if (token == "compat" || token == "compatibility")
            {
                return UniversalRenderer::RenderProfile::eCompatibility;
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

        [[nodiscard]] std::optional<UniversalRenderer::RenderProfile>
        parseCliRenderProfile(std::span<const std::string> args,
                              bool&                        sawRenderProfileArg,
                              bool&                        invalidRenderProfileValue)
        {
            std::optional<UniversalRenderer::RenderProfile> parsed;
            for (size_t i = 0; i < args.size(); ++i)
            {
                const std::string_view     arg = args[i];
                constexpr std::string_view kRenderProfileEqPrefix {"--render-profile="};

                if (arg.starts_with(kRenderProfileEqPrefix))
                {
                    sawRenderProfileArg = true;
                    if (auto value = parseRenderProfileToken(arg.substr(kRenderProfileEqPrefix.size()));
                        value.has_value())
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

        struct DiagnosticsCliOptions
        {
            std::optional<bool> validation;
            std::optional<bool> debugMarkers;
            std::optional<bool> renderDoc;
        };

        [[nodiscard]] DiagnosticsCliOptions parseCliDiagnostics(std::span<const std::string> args)
        {
            DiagnosticsCliOptions parsed;
            for (const auto& argString : args)
            {
                const std::string_view arg = argString;
                if (arg == "--validation")
                {
                    parsed.validation = true;
                }
                else if (arg == "--no-validation")
                {
                    parsed.validation = false;
                }
                else if (arg == "--debug-markers")
                {
                    parsed.debugMarkers = true;
                }
                else if (arg == "--no-debug-markers")
                {
                    parsed.debugMarkers = false;
                }
                else if (arg == "--renderdoc")
                {
                    parsed.renderDoc = true;
                }
                else if (arg == "--no-renderdoc")
                {
                    parsed.renderDoc = false;
                }
            }
            return parsed;
        }
    } // namespace

#if defined(__ANDROID__)
    void DemoAppHost::setAndroidRuntimeContext(const platform::android::AndroidAppRuntimeContext& runtimeContext)
    {
        m_AndroidRuntimeContext = runtimeContext;

        // Install the builtin resource pack from the APK before the engine initializes (every Android
        // entry routes through here). The single-binary embed path (builtin_pack_mount.cpp) is a
        // no-op on Android because the AAssetManager isn't available at static-init time, so the pack
        // -- bundled into the APK assets as builtin.vpk -- is read here instead.
        if (runtimeContext.assetManager != nullptr && !builtin::hasSource())
        {
            if (AAsset* asset = AAssetManager_open(runtimeContext.assetManager, "builtin.vpk", AASSET_MODE_BUFFER))
            {
                const off_t            size = AAsset_getLength(asset);
                const void*            data = AAsset_getBuffer(asset);
                std::vector<std::byte> blob;
                if (data != nullptr && size > 0)
                {
                    blob.resize(static_cast<size_t>(size));
                    std::memcpy(blob.data(), data, static_cast<size_t>(size));
                }
                AAsset_close(asset);
                mountBuiltinPackFromBytes(std::move(blob));
            }
            else
            {
                __android_log_print(
                    ANDROID_LOG_ERROR, "VULTRA_CORE", "[DemoAppHost] builtin.vpk missing from APK assets");
            }
        }
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
        controller.fovY            = 60.0f;
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

        UniversalRenderer::RenderProfile universalRenderProfile = UniversalRenderer::RenderProfile::eDefault;
        {
            bool sawRenderProfileArg       = false;
            bool invalidRenderProfileValue = false;
            if (auto parsed = parseCliRenderProfile(commandLineArgs(), sawRenderProfileArg, invalidRenderProfileValue);
                parsed.has_value())
            {
                universalRenderProfile = *parsed;
            }

            if (invalidRenderProfileValue)
            {
                VULTRA_CORE_WARN("[DemoAppHost] Invalid render profile CLI value. Use "
                                 "--render-profile=(default|universal|compat|compatibility)");
            }
        }

        engine.ctx().config.window.title                   = demoWindowTitle();
        engine.ctx().config.window.resizable               = demoWindowResizable();
        engine.ctx().config.render.backendApi              = backendApi;
        engine.ctx().config.render.renderDeviceFeatureFlag = demoRenderDeviceFeatureFlag();
        engine.ctx().config.asset.asyncLoading             = false;
        engine.ctx().config.render.builtinShaderLibrary =
            universalRenderProfile == UniversalRenderer::RenderProfile::eCompatibility ?
                EngineContext::Config::RenderConfig::BuiltinShaderLibrary::eCompatibility :
                EngineContext::Config::RenderConfig::BuiltinShaderLibrary::eAuto;
        const bool xrRuntimeCameraOverride =
            HasFlagValues(engine.ctx().config.render.renderDeviceFeatureFlag, rhi::RenderDeviceFeatureFlagBits::eXR);
        engine.ctx().config.render.xr.runtimeCameraOverride = xrRuntimeCameraOverride;

        const auto diagnostics = parseCliDiagnostics(commandLineArgs());
        if (diagnostics.validation.has_value())
        {
            engine.ctx().config.render.enableValidation = *diagnostics.validation;
        }
        if (diagnostics.debugMarkers.has_value())
        {
            engine.ctx().config.render.enableDebugMarkers = *diagnostics.debugMarkers;
        }
        if (diagnostics.renderDoc.has_value())
        {
            engine.ctx().config.render.enableRenderDoc = *diagnostics.renderDoc;
        }

        onConfigureDemo(engine);

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
        engine.emplaceSubsystem<I18nSystem>();
        engine.emplaceSubsystem<TimingSystem>();
        engine.emplaceSubsystem<JobSystem>();

        auto renderer = makeRenderer();
        if (!renderer)
        {
            renderer = createRef<UniversalRenderer>();
        }
        if (auto universalRenderer = std::dynamic_pointer_cast<UniversalRenderer>(renderer))
        {
            universalRenderer->setRenderProfile(universalRenderProfile);
        }

        auto& cameraSystem  = engine.emplaceSubsystem<CameraSystem>();
        auto  fpsController = makeFPSCameraController();
        auto& camera        = cameraSystem.addManualCamera({.rendererKey = renderer->name().data()});
        camera.name              = "Demo Runtime Camera";
        camera.xrViewEnabled     = xrRuntimeCameraOverride;
        camera.xrFallbackMono    = true;
        camera.isXRPrimaryView   = true;
        camera.renderImGui       = true;

        const float width  = static_cast<float>(std::max(engine.ctx().config.window.width, 1u));
        const float height = static_cast<float>(std::max(engine.ctx().config.window.height, 1u));
        const float aspect = width / height;

        const glm::vec3 forward = makeForward(fpsController.yawDegrees, fpsController.pitchDegrees);
        camera.view       = glm::lookAt(fpsController.position, fpsController.position + forward, glm::vec3(0, 1, 0));
        camera.projection = glm::perspectiveRH_ZO(
            glm::radians(fpsController.fovY), aspect, fpsController.zNear, fpsController.zFar);
        camera.fovY  = glm::radians(fpsController.fovY);
        camera.zNear = fpsController.zNear;
        camera.zFar  = fpsController.zFar;

        cameraSystem.setFPSCameraController(fpsController);

        engine.emplaceSubsystem<WorldSystem>();
        engine.emplaceSubsystem<UiSystem>();
        engine.emplaceSubsystem<PhysicsSystem>();

#if !defined(__EMSCRIPTEN__)
        engine.emplaceSubsystem<FrameDebuggerSystem>();
#endif
        engine.emplaceSubsystem<ShaderSystem>();
        engine.emplaceSubsystem<RenderBackendExtensionSystem>();
        engine.emplaceSubsystem<RenderUpscalerSystem>();
        engine.emplaceSubsystem<RenderBackendSystem>();
#if defined(VULTRA_ENABLE_XR) && VULTRA_ENABLE_XR
        engine.emplaceSubsystem<XRRuntimeSystem>();
#endif
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
            engine.emplaceSubsystem<GpuResourceSystem>();
            engine.emplaceSubsystem<AssetSystem>();
            engine.emplaceSubsystem<SceneSystem>();
            // Before ScriptSystem: it captures IAudioService into the Lua ScriptContext in onInit.
            engine.emplaceSubsystem<AudioSystem>();
            engine.emplaceSubsystem<ScriptSystem>();
            engine.emplaceSubsystem<AnimationSystem>();
            // After ScriptSystem so plugins can use the shared Lua state and the asset/scene services.
            engine.emplaceSubsystem<PluginSystem>();
            // Before RenderSystem so emitted particles queue their debug-draw preview for this frame.
            engine.emplaceSubsystem<ParticleSystem>();

            auto& renderSystem = engine.emplaceSubsystem<RenderSystem>();
            renderSystem.registerRenderer(renderer);
            renderSystem.registerRenderer(createRef<UniversalRtRenderer>());
        }
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

        if (e.type == event::WindowEventType::eCloseRequested || e.type == event::WindowEventType::eQuit)
        {
            m_PendingResize = false;
            return;
        }

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

            m_PendingResize       = true;
            m_PendingResizeWidth  = framebufferWidth;
            m_PendingResizeHeight = framebufferHeight;
            m_LastResizeEventTime = std::chrono::steady_clock::now();
        }
    }

    void DemoAppHost::applyPendingResize()
    {
        if (!m_PendingResize || m_PendingResizeWidth == 0u || m_PendingResizeHeight == 0u)
            return;

        auto* windowService = engineCtx().services.tryGet<IWindowService>();
        if (windowService != nullptr && windowService->window().shouldClose())
        {
            m_PendingResize = false;
            return;
        }

        constexpr auto kResizeSettleDelay = std::chrono::milliseconds(120);
        if (std::chrono::steady_clock::now() - m_LastResizeEventTime < kResizeSettleDelay)
            return;

        auto* backendService = engineCtx().services.tryGet<IRenderBackendService>();
        if (backendService == nullptr)
            return;

        const auto swapchainExtent = backendService->swapchain().getExtent();
        if (swapchainExtent.width == m_PendingResizeWidth && swapchainExtent.height == m_PendingResizeHeight)
        {
            m_PendingResize = false;
            return;
        }

        const uint32_t framebufferWidth  = m_PendingResizeWidth;
        const uint32_t framebufferHeight = m_PendingResizeHeight;
        m_PendingResize                  = false;

        if (auto* upscalerService = engineCtx().services.tryGet<IRenderUpscalerService>())
            upscalerService->onResize(rhi::Extent2D {framebufferWidth, framebufferHeight});

        backendService->renderDevice().waitIdle();
        backendService->frameController().recreate();

        if (auto* renderService = engineCtx().services.tryGet<IRenderService>(); renderService != nullptr)
            renderService->onResize(framebufferWidth, framebufferHeight);
    }

    void DemoAppHost::onPollEvents()
    {
        auto&     window        = engineCtx().services.require<IWindowService>().window();
        const int timeoutMillis = (!window.isReady()) ? -1 : 0;
        window.pollEvents(timeoutMillis);
        applyPendingResize();
    }

    bool DemoAppHost::onShouldClose() const
    {
        auto& window               = engineCtx().services.require<IWindowService>().window();
        auto* renderBackendService = engineCtx().services.tryGet<IRenderBackendService>();
        return window.shouldClose() || (renderBackendService && renderBackendService->isExitRequested());
    }
} // namespace vultra
