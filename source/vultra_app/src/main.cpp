#include "app_state.hpp"
#include "editor_app/editor_app.hpp"
#include "launch_options.hpp"
#include "project_launcher/project_launcher.hpp"
#include "vproject.hpp"

#include <vultra/core/app/demo_app_host.hpp>
#include <vultra/core/base/common_context.hpp>
#include <vultra/core/services/window_service.hpp>
#include <vultra/function/rendering/render_structs.hpp>
#include <vultra/function/rendering/srp/renderer.hpp>
#include <vultra/function/rendering/srp/builtin/features/compatibility_basecolor_feature.hpp>
#include <vultra/function/rendering/srp/builtin/features/final_composition_feature.hpp>
#include <vultra/function/rendering/srp/builtin/features/general_gaussian_splat_feature.hpp>
#include <vultra/function/rendering/srp/builtin/features/meshlet_feature.hpp>
#include <vultra/function/services/scene_service.hpp>
#include <vultra/function/services/camera_service.hpp>
#include <vultra/function/services/render_backend_service.hpp>
#include <vultra/function/services/world_service.hpp>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include <filesystem>
#include <algorithm>
#include <optional>
#include <string>
#include <vector>

namespace
{
    class VultraShellRenderer final : public vultra::FeatureRenderer
    {
    public:
        VultraShellRenderer(vultra_app::AppState&        state,
                            vultra_app::ProjectLauncher& launcher,
                            vultra_app::EditorApp&       editor) :
            m_State(state), m_Launcher(launcher), m_Editor(editor)
        {
        }

        std::string_view name() const override { return "universal"; }

        void init() override
        {
            auto* services = getServices();
            if (!services)
                return;

            const auto backendApi = services->require<vultra::IRenderBackendService>().renderDevice().getBackendApi();
            if (backendApi == vultra::rhi::RenderBackendApi::eWebGPU)
            {
                emplaceFeature<vultra::CompatibilityBaseColorFeature>();
                emplaceFeature<vultra::GeneralGaussianSplatFeature>();
                emplaceFeature<vultra::FinalCompositionFeature>();
                return;
            }

            emplaceFeature<vultra::MeshletFeature>();
            emplaceFeature<vultra::GeneralGaussianSplatFeature>();
            emplaceFeature<vultra::FinalCompositionFeature>();
        }

        void onImGui() override
        {
            if (auto* services = getServices())
            {
                auto* cameraService = services->tryGet<vultra::ICameraService>();
                auto* windowService = services->tryGet<IWindowService>();
                if (cameraService && windowService)
                {
                    cameraService->clearManualCameras();

                    const auto extent = windowService->window().getExtent();
                    const float width  = static_cast<float>(std::max(extent.x, 1));
                    const float height = static_cast<float>(std::max(extent.y, 1));
                    vultra::RenderCamera shellCamera {};
                    shellCamera.name        = "Vultra Editor UI";
                    shellCamera.priority    = 1000;
                    shellCamera.view        = glm::lookAt(glm::vec3 {0.0f, 0.0f, 1.0f},
                                                   glm::vec3 {0.0f, 0.0f, 0.0f},
                                                   glm::vec3 {0.0f, 1.0f, 0.0f});
                    shellCamera.projection  = glm::perspectiveRH_ZO(glm::radians(60.0f), width / height, 0.1f, 1000.0f);
                    shellCamera.zNear       = 0.1f;
                    shellCamera.zFar        = 1000.0f;
                    shellCamera.fovY        = glm::radians(60.0f);
                    shellCamera.clearValue  = {0.018f, 0.02f, 0.026f, 1.0f};
                    shellCamera.renderImGui = true;
                    shellCamera.rendererKey = "universal";
                    cameraService->addManualCamera(shellCamera);
                }
            }

            if (m_State.mode == vultra_app::AppMode::Editor)
            {
                vultra_app::EditorContext ctx {.state = m_State, .services = getServices()};
                m_Editor.draw(ctx);
            }
            else
                m_Launcher.draw(m_State, getServices() ? getServices()->tryGet<IWindowService>() : nullptr);
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

        const auto extent = windowService->window().getExtent();
        const float width  = static_cast<float>(std::max(extent.x, 1));
        const float height = static_cast<float>(std::max(extent.y, 1));
        vultra::RenderCamera shellCamera {};
        shellCamera.name        = "Vultra Editor UI";
        shellCamera.priority    = 1000;
        shellCamera.view        = glm::lookAt(glm::vec3 {0.0f, 0.0f, 1.0f},
                                       glm::vec3 {0.0f, 0.0f, 0.0f},
                                       glm::vec3 {0.0f, 1.0f, 0.0f});
        shellCamera.projection  = glm::perspectiveRH_ZO(glm::radians(60.0f), width / height, 0.1f, 1000.0f);
        shellCamera.zNear       = 0.1f;
        shellCamera.zFar        = 1000.0f;
        shellCamera.fovY        = glm::radians(60.0f);
        shellCamera.clearValue  = {0.018f, 0.02f, 0.026f, 1.0f};
        shellCamera.renderImGui = true;
        shellCamera.rendererKey = "universal";
        cameraService->addManualCamera(shellCamera);
    }

    class VultraStandaloneApp final : public vultra::DemoAppHost
    {
    public:
        explicit VultraStandaloneApp(vultra_app::LaunchOptions options) : m_Options(std::move(options))
        {
            m_VpkPath = vultra_app::findDefaultVpk(m_Options);
            if (m_Options.editorMode)
            {
                m_State.mode = vultra_app::AppMode::Editor;
                if (!m_Options.projectPath.empty())
                {
                    m_State.currentProject = m_Options.projectPath;
                    if (auto project = vultra_app::loadVProject(m_Options.projectPath); project.has_value())
                    {
                        m_State.currentProject      = project->projectDir;
                        m_State.currentProjectName  = project->name;
                        m_State.currentAssetRoot    = project->assetRoot;
                        m_State.currentDefaultScene = project->defaultScene;
                        m_State.currentRenderPipeline = project->renderPipeline;
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
                engine.ctx().config.imgui.enableDocking = true;
                engine.ctx().config.imgui.imguiIniFile  = "vultra_editor_layout_v2.ini";
                engine.ctx().config.window.width        = m_Options.editorMode ? 640 : 1280;
                engine.ctx().config.window.height       = m_Options.editorMode ? 360 : 720;
                engine.ctx().config.window.resizable    = !m_Options.editorMode;
                engine.ctx().config.window.visible      = !m_Options.editorMode;
                engine.ctx().config.window.decorated =
                    !m_Options.editorMode &&
                    engine.ctx().config.render.backendApi == vultra::rhi::RenderBackendApi::eWebGPU;
            }

            if (m_Options.editorMode)
            {
                vultra_app::EditorApp::configureProject(engine, m_Options);
                engine.ctx().config.asset.enableImportScan = false;
                return;
            }

            if (m_VpkPath.has_value())
            {
                engine.ctx().config.asset.loadFromVPK = true;
                engine.ctx().config.asset.assetRoot   = "/";
                engine.ctx().config.asset.vpkFile     = m_VpkPath->generic_string();
                return;
            }

            vultra_app::ProjectLauncher::configureAssets(engine, m_Options);
        }

        void onPostConfigureDemo(vultra::Engine& engine) override
        {
            if (m_Options.editorMode)
            {
                vultra_app::EditorApp::logStartup(m_Options);
                return;
            }

            if (!m_VpkPath.has_value())
            {
                vultra_app::ProjectLauncher::logStartup();
                return;
            }

            auto& sceneService = engine.ctx().services.require<vultra::ISceneService>();
            auto& worldService = engine.ctx().services.require<vultra::IWorldService>();
            sceneService.instantiateScene(worldService.world(), m_Options.sceneUri);

            VULTRA_CLIENT_INFO("[Vultra] Loaded scene '{}' from VPK '{}'", m_Options.sceneUri, m_VpkPath->generic_string());
        }

        void onBeforeEngineTick(vultra::fsec /*dt*/) override
        {
            if (m_State.mode == vultra_app::AppMode::Runtime)
                return;

            vultra_app::EditorContext ctx {.state = m_State, .services = &engineCtx().services};
            if (auto* cameraService = engineCtx().services.tryGet<vultra::ICameraService>())
                cameraService->setWorldCamerasEnabled(false);

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

        vultra_app::LaunchOptions             m_Options;
        std::optional<std::filesystem::path>  m_VpkPath;
        mutable vultra_app::AppState          m_State;
        mutable vultra_app::ProjectLauncher   m_Launcher;
        mutable vultra_app::EditorApp         m_Editor;
    };
} // namespace

int main(int argc, char** argv)
{
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
