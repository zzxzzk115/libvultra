#include "app_state.hpp"
#include "editor_app/editor_app.hpp"
#include "editor_app/editor_settings_persistence.hpp"
#include "launch_options.hpp"
#include "project_launcher/project_launcher.hpp"
#include "vproject.hpp"

#include <builtin_shaders.hpp>
#include <vasset/vpk.hpp>

#include <vasset/tool_cli.hpp>
#include <vasset/vasset_importers.hpp>
#include <vshadersystem/tool_cli.hpp>

#include <vultra/core/app/demo_app_host.hpp>
#include <vultra/core/base/common_context.hpp>
#include <vultra/core/services/window_service.hpp>
#include <vultra/function/rendering/render_structs.hpp>
#include <vultra/function/rendering/srp/builtin/universal_renderer.hpp>
#include <vultra/function/rendering/srp/builtin/universal_rt_renderer.hpp>
#include <vultra/function/rendering/srp/renderer.hpp>
#include <vultra/function/services/camera_service.hpp>
#include <vultra/function/services/render_backend_service.hpp>
#include <vultra/function/services/render_service.hpp>
#include <vultra/function/services/scene_service.hpp>
#include <vultra/function/services/world_service.hpp>
#include <vultra/function/world/components/camera_component.hpp>

#include <vbase/core/scoped_enum_flags.hpp>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace
{
    vasset::VAssetImporter::ImportOptions makeToolAssetImportOptions()
    {
        vasset::VAssetImporter::ImportOptions options;
        options.shaderVirtualIncludes.reserve(builtin_shader_include_sources_count);
        for (size_t i = 0; i < builtin_shader_include_sources_count; ++i)
        {
            const auto& source = builtin_shader_include_sources[i];
            options.shaderVirtualIncludes.push_back({
                .virtualPath = source.path,
                .sourceText  = std::string(reinterpret_cast<const char*>(source.data), source.size),
            });
        }
        return options;
    }

    bool dispatchToolCommand(int argc, char** argv, int& exitCode)
    {
        if (argc <= 1 || argv == nullptr || argv[1] == nullptr)
            return false;

        const std::string_view command {argv[1]};
        if (command == "asset" || command == "vasset" || command == "vasset-cli")
        {
            exitCode = vasset::tool::run_vasset_cli(argc - 1, argv + 1, makeToolAssetImportOptions());
            return true;
        }
        if (command == "shader" || command == "vshaderc")
        {
            exitCode = vshadersystem::tool::run_vshaderc(argc - 1, argv + 1);
            return true;
        }

        return false;
    }

    std::string_view stripResScheme(std::string_view uri)
    {
        constexpr std::string_view kScheme = "res://";
        if (uri.starts_with(kScheme))
            uri.remove_prefix(kScheme.size());
        while (!uri.empty() && uri.front() == '/')
            uri.remove_prefix(1);
        return uri;
    }

    std::string rendererKeyFromRenderGraphUri(std::string_view uri)
    {
        auto filename = std::filesystem::path(std::string(stripResScheme(uri))).filename().generic_string();
        constexpr std::string_view suffix = ".vrg.json";
        if (filename.ends_with(suffix))
            filename.resize(filename.size() - suffix.size());
        return filename.empty() ? "custom" : filename;
    }

    std::vector<std::string> renderGraphUrisFromVpkRecords(const vasset::VpkReadOnly& vpk)
    {
        std::vector<std::string> uris;
        for (const auto& entry : vpk.registry)
        {
            if (entry.type != vasset::VAssetType::eRenderGraphJson)
                continue;
            if (entry.pathOffset + entry.pathSize > vpk.stringTable.size())
                continue;

            std::string logicalPath {vpk.stringTable.data() + entry.pathOffset, entry.pathSize};
            std::replace(logicalPath.begin(), logicalPath.end(), '\\', '/');
            if (logicalPath.starts_with("res://"))
                uris.push_back(std::move(logicalPath));
            else
                uris.push_back("res://" + logicalPath);
        }

        std::sort(uris.begin(), uris.end());
        uris.erase(std::unique(uris.begin(), uris.end()), uris.end());
        return uris;
    }

    class VultraShellRenderer final : public vultra::FeatureRenderer
    {
    public:
        VultraShellRenderer(vultra_app::AppState&        state,
                            vultra_app::ProjectLauncher& launcher,
                            vultra_app::EditorApp& editor) : m_State(state), m_Launcher(launcher), m_Editor(editor)
        {}

        std::string_view name() const override { return "editor-shell"; }

        void init() override {}

        [[nodiscard]] bool usesFrameGraph() const override { return false; }

        void onImGui() override
        {
            if (auto* services = getServices())
            {
                auto* cameraService        = services->tryGet<vultra::ICameraService>();
                auto* windowService        = services->tryGet<IWindowService>();
                auto  addEditorShellCamera = [&]() {
                    if (!cameraService || !windowService)
                        return;

                    const auto           extent = windowService->window().getExtent();
                    const float          width  = static_cast<float>(std::max(extent.x, 1));
                    const float          height = static_cast<float>(std::max(extent.y, 1));
                    vultra::RenderCamera shellCamera {};
                    shellCamera.name     = "Vultra Editor UI";
                    shellCamera.priority = 1000;
                    shellCamera.view     = glm::lookAt(
                        glm::vec3 {0.0f, 0.0f, 1.0f}, glm::vec3 {0.0f, 0.0f, 0.0f}, glm::vec3 {0.0f, 1.0f, 0.0f});
                    shellCamera.projection = glm::perspectiveRH_ZO(glm::radians(60.0f), width / height, 0.1f, 1000.0f);
                    shellCamera.zNear      = 0.1f;
                    shellCamera.zFar       = 1000.0f;
                    shellCamera.fovY       = glm::radians(60.0f);
                    shellCamera.clearValue = {0.018f, 0.02f, 0.026f, 1.0f};
                    shellCamera.renderImGui = true;
                    shellCamera.rendererKey = "editor-shell";
                    cameraService->addManualCamera(shellCamera);
                };

                if (cameraService)
                    cameraService->clearManualCameras();

                if (m_State.mode == vultra_app::AppMode::Editor)
                {
                    vultra_app::EditorContext ctx {.state = m_State, .services = services};
                    m_Editor.draw(ctx);
                }
                else
                    m_Launcher.draw(m_State, windowService);

                addEditorShellCamera();
                return;
            }

            m_Launcher.draw(m_State, nullptr);
        }

    private:
        vultra_app::AppState&        m_State;
        vultra_app::ProjectLauncher& m_Launcher;
        vultra_app::EditorApp&       m_Editor;
    };

    void addShellCamera(vbase::ServiceRegistry& services)
    {
        auto* cameraService = services.tryGet<vultra::ICameraService>();
        auto* windowService = services.tryGet<IWindowService>();
        if (!cameraService || !windowService)
            return;

        const auto           extent = windowService->window().getExtent();
        const float          width  = static_cast<float>(std::max(extent.x, 1));
        const float          height = static_cast<float>(std::max(extent.y, 1));
        vultra::RenderCamera shellCamera {};
        shellCamera.name     = "Vultra Editor UI";
        shellCamera.priority = 1000;
        shellCamera.view =
            glm::lookAt(glm::vec3 {0.0f, 0.0f, 1.0f}, glm::vec3 {0.0f, 0.0f, 0.0f}, glm::vec3 {0.0f, 1.0f, 0.0f});
        shellCamera.projection  = glm::perspectiveRH_ZO(glm::radians(60.0f), width / height, 0.1f, 1000.0f);
        shellCamera.zNear       = 0.1f;
        shellCamera.zFar        = 1000.0f;
        shellCamera.fovY        = glm::radians(60.0f);
        shellCamera.clearValue  = {0.018f, 0.02f, 0.026f, 1.0f};
        shellCamera.renderImGui = true;
        shellCamera.rendererKey = "editor-shell";
        cameraService->addManualCamera(shellCamera);
    }

    void registerEditorSceneRenderer(vbase::ServiceRegistry& services)
    {
        auto* renderService = services.tryGet<vultra::IRenderService>();
        if (!renderService)
            return;

        renderService->registerRenderer(vultra::createRef<vultra::UniversalRenderer>());
        renderService->registerRenderer(vultra::createRef<vultra::UniversalRtRenderer>());
    }

    class VultraStandaloneApp final : public vultra::DemoAppHost
    {
    public:
        explicit VultraStandaloneApp(vultra_app::LaunchOptions options) : m_Options(std::move(options))
        {
            std::string settingsError;
            if (!vultra_app::loadEditorSettings(m_State.editorSettingsFile, m_State.editorSettings, &settingsError))
                m_State.statusMessage = "Editor settings load failed: " + settingsError;

            m_VpkPath = vultra_app::findDefaultVpk(m_Options);
            if (m_Options.editorMode)
            {
                m_State.mode = vultra_app::AppMode::Editor;
                if (!m_Options.projectPath.empty())
                {
                    m_State.currentProject = m_Options.projectPath;
                    if (auto project = vultra_app::loadVProject(m_Options.projectPath); project.has_value())
                    {
                        m_State.currentProject            = project->projectDir;
                        m_State.currentProjectName        = project->name;
                        m_State.currentAssetRoot          = project->assetRoot;
                        m_State.currentDefaultScene       = project->defaultScene;
                        m_State.currentEditingRenderGraph = project->editingRenderGraph;
                    }
                }
            }
            else if (m_VpkPath.has_value())
            {
                m_State.mode = vultra_app::AppMode::Runtime;
            }
            else
            {
                m_State.mode = vultra_app::AppMode::Launcher;
            }
        }

    private:
        std::string_view demoWindowTitle() const override
        {
            if (m_Options.editorMode)
                return vultra_app::EditorApp::kWindowTitle;
            if (!m_VpkPath.has_value())
                return vultra_app::ProjectLauncher::kWindowTitle;
            return "VultraEngine";
        }

        bool demoEnableExperimentalWebGPUContent() const override { return true; }

        vultra::FPSCameraController makeFPSCameraController() const override
        {
            auto controller = vultra::DemoAppHost::makeFPSCameraController();
            if (m_State.mode != vultra_app::AppMode::Runtime)
            {
                controller.enabled      = false;
                controller.captureMouse = false;
            }
            return controller;
        }

        vultra::Ref<vultra::Renderer> makeRenderer() const override
        {
            if (m_State.mode == vultra_app::AppMode::Runtime)
                return vultra::DemoAppHost::makeRenderer();

            return vultra::createRef<VultraShellRenderer>(m_State, m_Launcher, m_Editor);
        }

        void onConfigureDemo(vultra::Engine& engine) override
        {
            if (engine.ctx().config.render.backendApi == vultra::rhi::RenderBackendApi::eVulkan)
            {
                engine.ctx().config.render.renderDeviceFeatureFlag =
                    vultra::rhi::RenderDeviceFeatureFlagBits::eRayTracingPipeline;
            }

            const bool shouldRequestXR = m_Options.xr.value_or(true);
            if (shouldRequestXR)
            {
                if (engine.ctx().config.render.backendApi == vultra::rhi::RenderBackendApi::eVulkan)
                {
                    engine.ctx().config.render.renderDeviceFeatureFlag =
                        engine.ctx().config.render.renderDeviceFeatureFlag |
                        vultra::rhi::RenderDeviceFeatureFlagBits::eXR;
                }
                else
                {
                    if (m_Options.xr.value_or(false))
                        VULTRA_CLIENT_WARN("[Vultra] --xr requested, but XR is only supported on the Vulkan backend.");
                }
            }
            else
            {
                engine.ctx().config.render.renderDeviceFeatureFlag =
                    engine.ctx().config.render.renderDeviceFeatureFlag & ~vultra::rhi::RenderDeviceFeatureFlagBits::eXR;
            }
            if (m_Options.xrMirror.has_value())
                engine.ctx().config.render.xr.mirror = *m_Options.xrMirror;

            if (m_Options.validation.has_value())
            {
                engine.ctx().config.render.enableValidation = *m_Options.validation;
            }
            if (m_Options.debugMarkers.has_value())
            {
                engine.ctx().config.render.enableDebugMarkers = *m_Options.debugMarkers;
            }
            if (m_Options.renderDoc.has_value())
            {
                engine.ctx().config.render.enableRenderDoc = *m_Options.renderDoc;
            }

            if (m_State.mode != vultra_app::AppMode::Runtime)
            {
                engine.ctx().config.imgui.enableDocking                 = true;
                engine.ctx().config.imgui.imguiIniFile                  = "vultra_editor_layout_v2.ini";
                engine.ctx().config.window.width                        = m_Options.editorMode ? 640 : 1280;
                engine.ctx().config.window.height                       = m_Options.editorMode ? 360 : 720;
                engine.ctx().config.window.resizable                    = !m_Options.editorMode;
                engine.ctx().config.window.visible                      = !m_Options.editorMode;
                engine.ctx().config.window.decorated =
                    !m_Options.editorMode &&
                    engine.ctx().config.render.backendApi == vultra::rhi::RenderBackendApi::eWebGPU;
            }

            if (m_Options.editorMode)
            {
                vultra_app::EditorApp::configureProject(engine, m_Options);
                return;
            }

            if (m_VpkPath.has_value())
            {
                engine.ctx().config.asset.loadFromVPK = true;
                engine.ctx().config.asset.assetRoot   = "/";
                engine.ctx().config.asset.vpkFile     = m_VpkPath->generic_string();
                std::string manifestError;
                if (auto manifest = vultra_app::loadVPackageManifestFromVpk(*m_VpkPath, &manifestError);
                    manifest.has_value())
                {
                    if (!manifest->name.empty())
                        engine.ctx().config.window.title = manifest->name;
                    if (m_Options.sceneUri.empty())
                        m_Options.sceneUri = manifest->entryScene;
                }
                else
                {
                    VULTRA_CLIENT_WARN("[Vultra] VPK package manifest unavailable: {}", manifestError);
                }
                return;
            }

            vultra_app::ProjectLauncher::configureAssets(engine, m_Options);
        }

        void onPostConfigureDemo(vultra::Engine& engine) override
        {
            if (m_Options.editorMode)
            {
                registerEditorSceneRenderer(engine.ctx().services);
                vultra_app::EditorApp::logStartup(m_Options);
                return;
            }

            if (!m_VpkPath.has_value())
            {
                registerEditorSceneRenderer(engine.ctx().services);
                vultra_app::ProjectLauncher::logStartup();
                return;
            }

            auto* renderService = engine.ctx().services.tryGet<vultra::IRenderService>();
            if (renderService)
            {
                if (auto opened = vasset::openVpk(m_VpkPath->generic_string()); opened)
                {
                    for (const auto& uri : renderGraphUrisFromVpkRecords(opened.value()))
                    {
                        renderService->reloadRenderPipeline(uri, rendererKeyFromRenderGraphUri(uri));
                    }
                }
            }

            auto&             sceneService = engine.ctx().services.require<vultra::ISceneService>();
            auto&             worldService = engine.ctx().services.require<vultra::IWorldService>();
            const std::string sceneUri     = m_Options.sceneUri.empty() ? "res://scenes/main.vscn" : m_Options.sceneUri;
            sceneService.instantiateScene(worldService.world(), sceneUri);

            bool  hasSceneCamera = false;
            auto& world          = worldService.world();
            auto& reg            = world.registry();
            auto  view           = reg.view<vultra::CameraComponent>();
            for (auto entity : view)
            {
                hasSceneCamera = true;
                auto& camera   = view.get<vultra::CameraComponent>(entity);
                if (camera.rendererKey.empty())
                    camera.rendererKey = "universal";
            }
            if (hasSceneCamera)
            {
                if (auto* cameraService = engine.ctx().services.tryGet<vultra::ICameraService>())
                    cameraService->clearManualCameras();
            }

            VULTRA_CLIENT_INFO("[Vultra] Loaded scene '{}' from VPK '{}'", sceneUri, m_VpkPath->generic_string());
        }

        void onWindowEvent(const vultra::os::GeneralWindowEvent& e) override
        {
            vultra::DemoAppHost::onWindowEvent(e);

            if (m_State.mode != vultra_app::AppMode::Editor || e.type != vultra::event::WindowEventType::eFileDrop ||
                !e.fileDrop)
            {
                return;
            }

            for (const auto& path : e.fileDrop->paths)
            {
                if (!path.empty())
                    m_State.pendingExternalAssetDrops.emplace_back(std::filesystem::path(path).lexically_normal());
            }
        }

        void onBeforeEngineTick(vultra::fsec /*dt*/) override
        {
            if (m_State.mode == vultra_app::AppMode::Runtime)
                return;

            vultra_app::EditorContext ctx {.state = m_State, .services = &engineCtx().services};
            if (auto* cameraService = engineCtx().services.tryGet<vultra::ICameraService>())
            {
                cameraService->setWorldCamerasEnabled(false);
                cameraService->setWorldXRCamerasEnabled(true);
            }

            if (m_State.editorShutdownRequested && m_State.mode != vultra_app::AppMode::Editor)
            {
                m_Editor.shutdown(ctx);
                if (auto* cameraService = engineCtx().services.tryGet<vultra::ICameraService>())
                    cameraService->clearManualCameras();
                addShellCamera(engineCtx().services);
                return;
            }

            if (m_State.mode == vultra_app::AppMode::Editor)
                m_Editor.tick(ctx);
        }

        void onBeforeShutdown(vultra::Engine& engine) override
        {
            vultra_app::EditorContext ctx {.state = m_State, .services = &engine.ctx().services};
            m_Editor.shutdown(ctx);
        }

        vultra_app::LaunchOptions            m_Options;
        std::optional<std::filesystem::path> m_VpkPath;
        mutable vultra_app::AppState         m_State;
        mutable vultra_app::ProjectLauncher  m_Launcher;
        mutable vultra_app::EditorApp        m_Editor;
    };
} // namespace

int main(int argc, char** argv)
{
    int toolExitCode = 0;
    if (dispatchToolCommand(argc, argv, toolExitCode))
        return toolExitCode;

    std::vector<std::string> args;
    if (argc > 1 && argv != nullptr)
    {
        args.reserve(static_cast<size_t>(argc - 1));
        for (int i = 1; i < argc; ++i)
        {
            if (argv[i] != nullptr)
                args.emplace_back(argv[i]);
        }
    }

    auto options = vultra_app::parseLaunchOptions(args);
    if (options.showHelp)
    {
        vultra_app::printUsage();
        return 0;
    }
    if (options.cliOnly)
        return vultra_app::runCliOnly(options);

    VultraStandaloneApp app {std::move(options)};
    return app.run(argc, argv);
}
